#include "network/Connection.h"
#include <algorithm>
#include <array>
#include <utility>

namespace Lan {
Connection::Connection(std::unique_ptr<Platform::NetworkSocket> socket) : m_socket(std::move(socket)) {
    if(!m_socket) { m_alive=false; m_error="Missing network socket"; }
}
void Connection::fail(const std::string& error) { m_error=error; close(); }
void Connection::close() { m_alive=false; if(m_socket) m_socket->close(); m_send.clear(); m_queuedBytes=0; m_sendOffset=0; }
bool Connection::queue(Message message) {
    if(!m_alive) return false;
    try {
        auto bytes=encode(message);
        if(bytes.size()>MAX_QUEUED_BYTES-m_queuedBytes) { fail("Network send queue limit exceeded"); return false; }
        m_queuedBytes+=bytes.size(); m_send.push_back(std::move(bytes)); return true;
    } catch(const ProtocolError& error) { fail(error.what()); return false; }
}
bool Connection::pop(Message& message) { return m_decoder.pop(message); }
bool Connection::poll() {
    if(!m_alive) return false;
    if(m_remoteClosed) { fail("Connection closed"); return false; }
    // Per-frame byte budgets bound main-thread I/O, even for a peer that can
    // continuously refill its socket. Decoding and gameplay are separate.
    constexpr size_t budget=256*1024;
    size_t sent=0;
    while(!m_send.empty() && sent<budget) {
        const auto& bytes=m_send.front();
        auto result=m_socket->send(bytes.data()+m_sendOffset,std::min(bytes.size()-m_sendOffset,budget-sent));
        if(result.status==Platform::SocketStatus::Pending) break;
        if(result.status!=Platform::SocketStatus::Ready) { fail(m_socket->error().empty()?"Connection closed":m_socket->error()); return false; }
        sent+=result.transferred; m_sendOffset+=result.transferred; m_queuedBytes-=result.transferred;
        if(m_sendOffset==bytes.size()) { m_send.pop_front(); m_sendOffset=0; }
    }
    std::array<uint8_t,8192> buffer{};
    size_t received=0;
    try {
        while(received<budget) {
            auto result=m_socket->receive(buffer.data(),std::min(buffer.size(),budget-received));
            if(result.status==Platform::SocketStatus::Pending) break;
            if(result.status!=Platform::SocketStatus::Ready) {
                // Deliver frames read before EOF (including a rejection reason)
                // before reporting the disconnect on the next poll.
                if(received) { m_remoteClosed=true; break; }
                fail(m_socket->error().empty()?"Connection closed":m_socket->error()); return false;
            }
            received+=result.transferred; m_decoder.feed(buffer.data(),result.transferred);
        }
    } catch(const ProtocolError& error) { fail(error.what()); return false; }
    return true;
}
}
