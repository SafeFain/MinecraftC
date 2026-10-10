#pragma once

#include "network/Protocol.h"

namespace Lan {
enum class ChatKind : uint8_t { Message, Joined, Left };
struct ChatMessage {
    ChatKind kind = ChatKind::Message;
    std::string nickname, text;
};
inline Bytes encodeChat(const ChatMessage& message) {
    if (message.kind > ChatKind::Left || (message.kind == ChatKind::Message && message.text.empty()) ||
        (message.kind != ChatKind::Message && !message.text.empty())) throw ProtocolError("Invalid chat message");
    for (unsigned char character : message.text)
        if (character < 32 || character == 127) throw ProtocolError("Invalid chat text");
    Writer writer; writer.u8(1); writer.u8(static_cast<uint8_t>(message.kind));
    writer.text(message.nickname, 64); writer.text(message.text, 384);
    return std::move(writer.bytes);
}
inline ChatMessage decodeChat(const Bytes& bytes) {
    Reader reader(bytes);
    if (reader.u8() != 1) throw ProtocolError("Unknown chat schema");
    ChatMessage message; message.kind = static_cast<ChatKind>(reader.u8());
    message.nickname = reader.text(64); message.text = reader.text(384);
    reader.finish(); encodeChat(message); return message;
}
}
