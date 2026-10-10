#pragma once
#include "core/NetworkSocket.h"
#include "network/Protocol.h"
#include <deque>
#include <memory>

namespace Lan {
class Connection {
public:
    explicit Connection(std::unique_ptr<Platform::NetworkSocket> socket);
    bool queue(Message message);
    bool poll();
    bool pop(Message& message);
    void close();
    void closeWithReason(const std::string& reason);
    bool alive() const { return m_alive; }
    bool connected() { return m_alive && m_socket->connectionStatus()==Platform::SocketStatus::Ready; }
    size_t queuedBytes() const { return m_queuedBytes; }
    const std::string& error() const { return m_error; }
private:
    std::unique_ptr<Platform::NetworkSocket> m_socket;
    Decoder m_decoder;
    std::deque<Bytes> m_send;
    size_t m_sendOffset=0;
    size_t m_queuedBytes=0;
    bool m_alive=true;
    bool m_remoteClosed=false;
    std::string m_error;
    void fail(const std::string& error);
};
}
