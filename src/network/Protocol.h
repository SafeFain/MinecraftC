#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <string>
#include <vector>

namespace Lan {
constexpr uint16_t PROTOCOL_VERSION = 3;
constexpr uint16_t DEFAULT_PORT = 25565;
constexpr size_t MAX_PLAYERS = 8;
constexpr size_t MAX_PAYLOAD = 1024 * 1024;
constexpr size_t MAX_QUEUED_BYTES = 4 * MAX_PAYLOAD;
constexpr size_t MAX_MESSAGES_PER_POLL = 128;
using Bytes = std::vector<uint8_t>;

enum class MessageType : uint16_t {
    Hello = 1, Welcome, Reject, BindChunks, Ping, Pong, Leave,
    Input, Action, ActionResult, PlayerState, EntityState, ChunkRequest,
    ChunkSnapshot, ChunkDelta, Environment, Inventory, Container, Chat,
    Dimension, Event, LodRequest, LodColumns, LodInvalidate
};
struct Message {
    MessageType type = MessageType::Ping;
    uint64_t sequence = 0;
    Bytes payload;
};
class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
class Writer {
public:
    Bytes bytes;
    void u8(uint8_t value);
    void u16(uint16_t value);
    void u32(uint32_t value);
    void u64(uint64_t value);
    void f32(float value);
    void f64(double value);
    void text(const std::string& value, size_t maximum = 256);
    void raw(const Bytes& value);
};
class Reader {
public:
    explicit Reader(const Bytes& bytes) : m_bytes(bytes) {}
    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    uint64_t u64();
    float f32();
    double f64();
    std::string text(size_t maximum = 256);
    size_t remaining() const { return m_bytes.size() - m_offset; }
    void finish() const;
private:
    const Bytes& m_bytes;
    size_t m_offset = 0;
    void require(size_t count) const;
};
Bytes encode(const Message& message);
// Incremental bounded TCP parser. The caller drains ready frames before feeding
// more data; partial frames survive reads without assuming socket boundaries.
class Decoder {
public:
    void feed(const uint8_t* data, size_t size);
    bool pop(Message& message);
    size_t bufferedBytes() const { return m_buffer.size() + m_readyBytes; }
private:
    Bytes m_buffer;
    std::deque<Message> m_ready;
    size_t m_readyBytes = 0;
};
}
