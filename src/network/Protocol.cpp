#include "network/Protocol.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace Lan {
namespace {
constexpr uint32_t MAGIC = 0x434e414c; // LANC in little-endian wire order.
constexpr size_t HEADER_SIZE = 20;
bool validUtf8(const std::string& value) {
    for (size_t offset = 0; offset < value.size();) {
        const auto first = static_cast<uint8_t>(value[offset++]);
        if (first < 0x80) continue;
        uint32_t codepoint = 0, minimum = 0;
        size_t continuation = 0;
        if (first >= 0xc2 && first <= 0xdf) {codepoint=first&0x1f;minimum=0x80;continuation=1;}
        else if (first >= 0xe0 && first <= 0xef) {codepoint=first&0x0f;minimum=0x800;continuation=2;}
        else if (first >= 0xf0 && first <= 0xf4) {codepoint=first&0x07;minimum=0x10000;continuation=3;}
        else return false;
        if (continuation > value.size() - offset) return false;
        for (size_t i=0;i<continuation;++i) {
            const auto byte=static_cast<uint8_t>(value[offset++]);
            if ((byte&0xc0)!=0x80) return false;
            codepoint=(codepoint<<6)|(byte&0x3f);
        }
        if (codepoint < minimum || codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    }
    return true;
}
bool validType(uint16_t type) {
    return type >= static_cast<uint16_t>(MessageType::Hello) &&
           type <= static_cast<uint16_t>(MessageType::Event);
}
}
void Writer::u8(uint8_t value) { bytes.push_back(value); }
void Writer::u16(uint16_t value) { for (int i=0;i<2;++i) u8(static_cast<uint8_t>(value >> (8*i))); }
void Writer::u32(uint32_t value) { for (int i=0;i<4;++i) u8(static_cast<uint8_t>(value >> (8*i))); }
void Writer::u64(uint64_t value) { for (int i=0;i<8;++i) u8(static_cast<uint8_t>(value >> (8*i))); }
void Writer::f32(float value) {
    static_assert(sizeof(float)==4 && std::numeric_limits<float>::is_iec559);
    if (!std::isfinite(value)) throw ProtocolError("Non-finite network float");
    uint32_t bits; std::memcpy(&bits,&value,4); u32(bits);
}
void Writer::f64(double value) {
    static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559);
    if (!std::isfinite(value)) throw ProtocolError("Non-finite network double");
    uint64_t bits; std::memcpy(&bits,&value,8); u64(bits);
}
void Writer::text(const std::string& value, size_t maximum) {
    if (value.size()>maximum || value.size()>65535 || value.find('\0')!=std::string::npos || !validUtf8(value))
        throw ProtocolError("Invalid network string");
    u16(static_cast<uint16_t>(value.size())); bytes.insert(bytes.end(),value.begin(),value.end());
}
void Writer::raw(const Bytes& value) { bytes.insert(bytes.end(),value.begin(),value.end()); }
void Reader::require(size_t count) const {
    if (count>remaining()) throw ProtocolError("Truncated network payload");
}
uint8_t Reader::u8() { require(1); return m_bytes[m_offset++]; }
uint16_t Reader::u16() { uint16_t result=0; for(int i=0;i<2;++i) result|=static_cast<uint16_t>(u8())<<(8*i); return result; }
uint32_t Reader::u32() { uint32_t result=0; for(int i=0;i<4;++i) result|=static_cast<uint32_t>(u8())<<(8*i); return result; }
uint64_t Reader::u64() { uint64_t result=0; for(int i=0;i<8;++i) result|=static_cast<uint64_t>(u8())<<(8*i); return result; }
float Reader::f32() { uint32_t bits=u32(); float value; std::memcpy(&value,&bits,4); if(!std::isfinite(value)) throw ProtocolError("Non-finite network float"); return value; }
double Reader::f64() { uint64_t bits=u64(); double value; std::memcpy(&value,&bits,8); if(!std::isfinite(value)) throw ProtocolError("Non-finite network double"); return value; }
std::string Reader::text(size_t maximum) {
    const size_t count=u16();
    if(count>maximum) throw ProtocolError("Network string limit exceeded");
    require(count);
    std::string value(m_bytes.begin()+m_offset,m_bytes.begin()+m_offset+count); m_offset+=count;
    if(value.find('\0')!=std::string::npos || !validUtf8(value)) throw ProtocolError("Invalid network string");
    return value;
}
void Reader::finish() const { if(remaining()) throw ProtocolError("Unexpected network payload bytes"); }
Bytes encode(const Message& message) {
    if(!validType(static_cast<uint16_t>(message.type)) || message.payload.size()>MAX_PAYLOAD)
        throw ProtocolError("Invalid network message");
    Writer writer; writer.u32(MAGIC); writer.u16(PROTOCOL_VERSION);
    writer.u16(static_cast<uint16_t>(message.type)); writer.u32(static_cast<uint32_t>(message.payload.size()));
    writer.u64(message.sequence); writer.raw(message.payload); return std::move(writer.bytes);
}
void Decoder::feed(const uint8_t* data, size_t size) {
    if(size>MAX_QUEUED_BYTES || bufferedBytes()>MAX_QUEUED_BYTES-size)
        throw ProtocolError("Network receive queue limit exceeded");
    if(size) m_buffer.insert(m_buffer.end(),data,data+size);
    size_t consumed=0;
    while(m_buffer.size()-consumed>=HEADER_SIZE) {
        Bytes header(m_buffer.begin()+consumed,m_buffer.begin()+consumed+HEADER_SIZE);
        Reader reader(header);
        if(reader.u32()!=MAGIC) throw ProtocolError("Invalid network magic");
        if(reader.u16()!=PROTOCOL_VERSION) throw ProtocolError("Incompatible LAN protocol");
        const auto type=reader.u16(); const size_t length=reader.u32(); const auto sequence=reader.u64();
        if(!validType(type) || length>MAX_PAYLOAD) throw ProtocolError("Invalid network frame");
        if(m_buffer.size()-consumed<HEADER_SIZE+length) break;
        if(m_ready.size()>=MAX_MESSAGES_PER_POLL) throw ProtocolError("Network message queue limit exceeded");
        Message message{static_cast<MessageType>(type),sequence,{}};
        message.payload.assign(m_buffer.begin()+consumed+HEADER_SIZE,m_buffer.begin()+consumed+HEADER_SIZE+length);
        m_readyBytes+=HEADER_SIZE+length; m_ready.push_back(std::move(message)); consumed+=HEADER_SIZE+length;
    }
    if(consumed) m_buffer.erase(m_buffer.begin(),m_buffer.begin()+consumed);
}
bool Decoder::pop(Message& message) {
    if(m_ready.empty()) return false;
    message=std::move(m_ready.front()); m_ready.pop_front(); m_readyBytes-=HEADER_SIZE+message.payload.size(); return true;
}
}
