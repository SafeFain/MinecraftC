#pragma once
#include "network/Connection.h"
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace Lan {
struct Compatibility {
    std::string gameVersion;
    uint32_t generationVersion=0;
    uint64_t contentSignature=0;
    std::map<std::string,std::string> content;
};
struct Identity {
    std::string id;
    std::string credential;
    std::string nickname;
};
struct Peer {
    uint64_t id=0;
    Identity identity;
    std::string token;
    std::unique_ptr<Connection> control;
    std::unique_ptr<Connection> chunks;
    uint64_t receivedControl=0, receivedChunks=0;
    uint64_t sentControl=0, sentChunks=0;
    double lastReceive=0;
    double lastPing=0;
};
struct Received {
    uint64_t peer=0;
    bool chunks=false;
    Message message;
};
struct SessionEvents {
    std::vector<uint64_t> joined;
    std::vector<uint64_t> left;
    std::vector<Received> messages;
};
class Host {
public:
    bool open(uint16_t port, const Compatibility& compatibility, size_t capacity=MAX_PLAYERS, bool loopbackOnly=false);
    void close(const std::string& reason={});
    SessionEvents poll(double now);
    bool send(uint64_t peer, Message message, bool chunks=false);
    void disconnect(uint64_t peer);
    size_t capacity() const { return m_capacity; }
    uint16_t port() const { return m_listener.localPort(); }
    const std::map<uint64_t,Peer>& peers() const { return m_peers; }
    const std::string& error() const { return m_error; }
    // Called before admission. A false result must leave persistence unchanged.
    std::function<bool(const Identity&)> admit;
private:
    struct Pending { std::unique_ptr<Connection> connection; double started=0; bool reject=false; };
    Platform::NetworkSocket m_listener;
    Compatibility m_compatibility;
    size_t m_capacity=MAX_PLAYERS;
    std::vector<Pending> m_pending;
    std::map<uint64_t,Peer> m_peers;
    uint64_t m_nextPeer=1;
    std::string m_error;
    std::vector<uint64_t> m_departed;
    void reject(Pending& pending,const std::string& reason);
};
class Client {
public:
    enum class State { Idle, Connecting, Binding, Connected, Failed };
    bool join(const std::string& address,uint16_t port,const Compatibility& compatibility,const Identity& identity,double now);
    std::vector<Received> poll(double now);
    bool send(Message message,bool chunks=false);
    void close();
    State state() const { return m_state; }
    uint64_t peerId() const { return m_peer; }
    const std::string& error() const { return m_error; }
private:
    State m_state=State::Idle;
    Compatibility m_compatibility;
    std::unique_ptr<Connection> m_control,m_chunks;
    std::string m_address,m_error;
    uint16_t m_port=0;
    uint64_t m_peer=0;
    uint64_t m_receivedControl=0,m_receivedChunks=0;
    uint64_t m_sentControl=0,m_sentChunks=0;
    double m_started=0,m_lastReceive=0,m_lastPing=0,m_bulkFailedAt=-1;
    void fail(const std::string& reason);
};
uint64_t contentSignature(const std::map<std::string,std::string>& content);
std::string randomToken();
bool validIdentity(const Identity& identity);
}
