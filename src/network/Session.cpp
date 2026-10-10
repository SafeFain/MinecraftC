#include "network/Session.h"
#include <algorithm>
#include <random>
#include <utility>

namespace Lan {
namespace {
constexpr double HANDSHAKE_TIMEOUT=10;
constexpr double PEER_TIMEOUT=15;
constexpr double PING_INTERVAL=5;
bool hexToken(const std::string& value) {
    return value.size()==32 && std::all_of(value.begin(),value.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');});
}
bool applicationMessage(MessageType type) {
    return type == MessageType::Leave || (type >= MessageType::Input && type <= MessageType::LodInvalidate);
}
Message hello(const Compatibility& compatible,const Identity& identity) {
    Writer writer;writer.text(compatible.gameVersion,64);writer.u32(compatible.generationVersion);writer.u64(compatible.contentSignature);
    writer.text(identity.id,32);writer.text(identity.credential,32);writer.text(identity.nickname,64);
    return {MessageType::Hello,0,std::move(writer.bytes)};
}
}
std::string randomToken() {
    std::random_device random;static constexpr char hex[]="0123456789abcdef";
    std::string token;token.reserve(32);
    for(int i=0;i<16;++i) { const auto byte=static_cast<uint8_t>(random());token+=hex[byte>>4];token+=hex[byte&15]; }
    return token;
}
bool validIdentity(const Identity& identity) {
    return hexToken(identity.id) && hexToken(identity.credential) && !identity.nickname.empty() && identity.nickname.size()<=64 &&
        std::none_of(identity.nickname.begin(),identity.nickname.end(),[](unsigned char c){return c<32 || c==127;});
}
bool Host::open(uint16_t port,const Compatibility& compatibility,size_t capacity,bool loopbackOnly) {
    close();m_error.clear();
    if(capacity<2 || capacity>MAX_PLAYERS || compatibility.gameVersion.empty()) {m_error="Invalid LAN room configuration";return false;}
    if(!m_listener.listen(port,loopbackOnly)) {m_error=m_listener.error();return false;}
    m_compatibility=compatibility;m_capacity=capacity;return true;
}
void Host::close() {m_pending.clear();m_peers.clear();m_departed.clear();m_listener.close();}
void Host::reject(Pending& pending,const std::string& reason) {
    Writer writer;writer.text(reason);pending.connection->queue({MessageType::Reject,0,std::move(writer.bytes)});pending.reject=true;
}
void Host::disconnect(uint64_t peer) {
    if(m_peers.erase(peer)) m_departed.push_back(peer);
}
bool Host::send(uint64_t peer,Message message,bool chunks) {
    auto found=m_peers.find(peer);if(found==m_peers.end()) return false;
    auto& channel=chunks?found->second.chunks:found->second.control;
    if (!channel || !applicationMessage(message.type)) return false;
    auto& sequence=chunks?found->second.sentChunks:found->second.sentControl;
    if (sequence==UINT64_MAX) {channel->close();return false;}
    message.sequence=++sequence;
    return channel->queue(std::move(message));
}
SessionEvents Host::poll(double now) {
    SessionEvents events;events.left.swap(m_departed);
    if(!m_listener.localPort()) return events;
    // A fixed acceptance window and deadline prevent half-open connections
    // from consuming unbounded memory or stealing the whole gameplay frame.
    for(int i=0;i<8;++i) {
        auto socket=m_listener.accept();if(!socket) break;
        if(m_pending.size()>=16) {socket->close();continue;}
        m_pending.push_back({std::make_unique<Connection>(std::move(socket)),now,false});
    }
    for(auto it=m_pending.begin();it!=m_pending.end();) {
        auto& pending=*it;
        pending.connection->poll();
        if(!pending.connection->alive() || now-pending.started>HANDSHAKE_TIMEOUT ||
            (pending.reject && pending.connection->queuedBytes()==0)) {it=m_pending.erase(it);continue;}
        Message message;
        if(!pending.reject && pending.connection->pop(message)) {
            try {
                if(message.sequence!=0) throw ProtocolError("Invalid handshake sequence");
                Reader reader(message.payload);
                if(message.type==MessageType::Hello) {
                    const auto version=reader.text(64);const auto generation=reader.u32();const auto signature=reader.u64();
                    Identity identity{reader.text(32),reader.text(32),reader.text(64)};reader.finish();
                    if(version!=m_compatibility.gameVersion || generation!=m_compatibility.generationVersion || signature!=m_compatibility.contentSignature)
                        throw ProtocolError("Incompatible game, generation or content version");
                    if(!validIdentity(identity)) throw ProtocolError("Invalid player identity");
                    if(m_peers.size()+1>=m_capacity) throw ProtocolError("LAN room is full");
                    for(const auto& pair:m_peers) if(pair.second.identity.id==identity.id) throw ProtocolError("Player identity is already connected");
                    if(admit && !admit(identity)) throw ProtocolError("Player credential was rejected");
                    Peer peer;peer.id=m_nextPeer++;peer.identity=std::move(identity);peer.token=randomToken();peer.lastReceive=now;peer.lastPing=now;
                    Writer writer;writer.u64(peer.id);writer.text(peer.token,32);
                    pending.connection->queue({MessageType::Welcome,0,std::move(writer.bytes)});
                    peer.control=std::move(pending.connection);m_peers.emplace(peer.id,std::move(peer));
                    it=m_pending.erase(it);continue;
                } else if(message.type==MessageType::BindChunks) {
                    const auto id=reader.u64();const auto token=reader.text(32);reader.finish();
                    auto found=m_peers.find(id);
                    if(found==m_peers.end() || found->second.token!=token || found->second.chunks) throw ProtocolError("Invalid chunk connection token");
                    found->second.chunks=std::move(pending.connection);found->second.lastReceive=now;
                    found->second.chunks->queue({MessageType::BindChunks,0,{}});events.joined.push_back(id);
                    it=m_pending.erase(it);continue;
                } else throw ProtocolError("Expected LAN handshake");
            } catch(const ProtocolError& error) {reject(pending,error.what());}
        }
        ++it;
    }
    for(auto it=m_peers.begin();it!=m_peers.end();) {
        auto& peer=it->second;bool alive=peer.control->poll();
        if(peer.chunks) alive=peer.chunks->poll() && alive;
        if(!alive || now-peer.lastReceive>PEER_TIMEOUT) {events.left.push_back(peer.id);it=m_peers.erase(it);continue;}
        for(bool chunks:{false,true}) {
            auto& channel=chunks?peer.chunks:peer.control;if(!channel) continue;
            Message message;
            for(size_t count=0;count<MAX_MESSAGES_PER_POLL && channel->pop(message);++count) {
                peer.lastReceive=now;
                if(!chunks && message.type==MessageType::Ping && message.payload.empty()) channel->queue({MessageType::Pong,message.sequence,{}});
                else if(!chunks && message.type==MessageType::Pong && message.payload.empty()) {}
                else if(peer.chunks && applicationMessage(message.type)) {
                    auto& sequence=chunks?peer.receivedChunks:peer.receivedControl;
                    if (!message.sequence || message.sequence<=sequence) {channel->close();break;}
                    sequence=message.sequence;
                    events.messages.push_back({peer.id,chunks,std::move(message)});
                } else {channel->close();break;}
            }
        }
        if(now-peer.lastPing>=PING_INTERVAL) {peer.lastPing=now;peer.control->queue({MessageType::Ping,0,{}});}
        ++it;
    }
    return events;
}
void Client::close() {
    m_control.reset();m_chunks.reset();m_state=State::Idle;m_peer=0;
    m_receivedControl=m_receivedChunks=m_sentControl=m_sentChunks=0;
}
void Client::fail(const std::string& reason) {close();m_state=State::Failed;m_error=reason;}
bool Client::join(const std::string& address,uint16_t port,const Compatibility& compatibility,const Identity& identity,double now) {
    close();m_error.clear();
    if(!validIdentity(identity)) {fail("Invalid local player identity");return false;}
    auto socket=std::make_unique<Platform::NetworkSocket>();
    if(!socket->connect(address,port)) {fail(socket->error());return false;}
    m_address=address;m_port=port;m_started=m_lastReceive=m_lastPing=now;m_state=State::Connecting;
    m_control=std::make_unique<Connection>(std::move(socket));
    try {return m_control->queue(hello(compatibility,identity));} catch(const ProtocolError& error) {fail(error.what());return false;}
}
bool Client::send(Message message,bool chunks) {
    if(m_state!=State::Connected) return false;
    auto& channel=chunks?m_chunks:m_control;
    if (!channel || !applicationMessage(message.type)) return false;
    auto& sequence=chunks?m_sentChunks:m_sentControl;
    if (sequence==UINT64_MAX) {fail("LAN message sequence exhausted");return false;}
    message.sequence=++sequence;
    return channel->queue(std::move(message));
}
std::vector<Received> Client::poll(double now) {
    std::vector<Received> messages;
    if(m_state==State::Idle || m_state==State::Failed) return messages;
    if(m_state!=State::Connected && now-m_started>HANDSHAKE_TIMEOUT) {fail("LAN connection timed out");return messages;}
    if(!m_control->poll() || (m_chunks && !m_chunks->poll())) {fail("LAN connection closed");return messages;}
    try {
        for(bool chunks:{false,true}) {
            auto& channel=chunks?m_chunks:m_control;if(!channel) continue;
            Message message;
            for(size_t count=0;count<MAX_MESSAGES_PER_POLL && channel->pop(message);++count) {
                m_lastReceive=now;
                if(message.type==MessageType::Reject) {Reader reader(message.payload);auto reason=reader.text();reader.finish();fail(reason);return messages;}
                if(m_state==State::Connecting && !chunks && message.type==MessageType::Welcome && message.sequence==0) {
                    Reader reader(message.payload);m_peer=reader.u64();auto token=reader.text(32);reader.finish();
                    if(!m_peer || !hexToken(token)) throw ProtocolError("Invalid LAN welcome");
                    auto socket=std::make_unique<Platform::NetworkSocket>();if(!socket->connect(m_address,m_port)) {fail(socket->error());return messages;}
                    m_chunks=std::make_unique<Connection>(std::move(socket));Writer writer;writer.u64(m_peer);writer.text(token,32);
                    m_chunks->queue({MessageType::BindChunks,0,std::move(writer.bytes)});m_state=State::Binding;
                } else if(m_state==State::Binding && chunks && message.type==MessageType::BindChunks && message.payload.empty()) m_state=State::Connected;
                else if(!chunks && message.type==MessageType::Ping && message.payload.empty()) channel->queue({MessageType::Pong,message.sequence,{}});
                else if(!chunks && message.type==MessageType::Pong && message.payload.empty()) {}
                else if((m_state==State::Connected || m_state==State::Binding) && applicationMessage(message.type)) {
                    auto& sequence=chunks?m_receivedChunks:m_receivedControl;
                    if (!message.sequence || message.sequence<=sequence) throw ProtocolError("Invalid LAN message sequence");
                    sequence=message.sequence;
                    messages.push_back({m_peer,chunks,std::move(message)});
                } else throw ProtocolError("Unexpected LAN handshake message");
            }
        }
    } catch(const ProtocolError& error) {fail(error.what());return {};}
    if(now-m_lastReceive>PEER_TIMEOUT) {fail("LAN host timed out");return messages;}
    if(now-m_lastPing>=PING_INTERVAL) {m_lastPing=now;m_control->queue({MessageType::Ping,0,{}});}
    return messages;
}
}
