// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 DAW Plugin Capture contributors
#include "ObsClient.h"

#include <juce_cryptography/juce_cryptography.h>

namespace pc
{
namespace
{
const char* const unreachableMessage = "OBS isn't reachable - is OBS open with Tools > WebSocket Server Settings > Enable ticked?";
const char* const needPasswordMessage = "OBS has authentication on - enter the WebSocket password (OBS: WebSocket Server Settings > Show Connect Info).";
const char* const badPasswordMessage = "OBS rejected the login - check the WebSocket password (OBS: WebSocket Server Settings > Show Connect Info).";
const char* const timeoutMessage = "Timed out talking to OBS - check host and port.";
const char* const notObsMessage = "That host/port isn't an OBS WebSocket server.";

constexpr int opText = 0x1, opClose = 0x8, opPing = 0x9, opPong = 0xA;
constexpr int closeAuthenticationFailed = 4009;
constexpr uint64_t maxMessageSize = 4 * 1024 * 1024;     // OBS replies are a few kilobytes

juce::String sha256Base64 (const juce::String& text)
{
    auto digest = juce::SHA256 (text.toUTF8()).getRawData();
    return juce::Base64::toBase64 (digest.getData(), digest.getSize());
}
} // namespace

juce::var J (std::initializer_list<std::pair<juce::Identifier, juce::var>> properties)
{
    auto* object = new juce::DynamicObject();
    for (auto& [key, value] : properties)
        object->setProperty (key, value);
    return juce::var (object);
}

ObsClient::ObsClient (const juce::String& host, int port, const juce::String& password, int timeout)
    : timeoutMs (timeout)
{
    // "localhost" may resolve to ::1 first, and on Windows a refused non-blocking connect then waits
    // for the full timeout before 127.0.0.1 is tried. OBS always listens on IPv4.
    auto address = host.trim().equalsIgnoreCase ("localhost") ? juce::String ("127.0.0.1") : host.trim();
    if (! socket.connect (address, port, timeoutMs))
        throw ObsError (ObsError::Unreachable, unreachableMessage);

    // WebSocket upgrade
    uint8_t keyBytes[16];
    juce::Random::getSystemRandom().fillBitsRandomly (keyBytes, sizeof (keyBytes));
    const juce::String upgrade = "GET / HTTP/1.1\r\n"
                                 "Host: " + host + ":" + juce::String (port) + "\r\n"
                                 "Upgrade: websocket\r\n"
                                 "Connection: Upgrade\r\n"
                                 "Sec-WebSocket-Key: " + juce::Base64::toBase64 (keyBytes, sizeof (keyBytes)) + "\r\n"
                                 "Sec-WebSocket-Version: 13\r\n"
                                 "Sec-WebSocket-Protocol: obswebsocket.json\r\n\r\n";
    writeAll (upgrade.toRawUTF8(), upgrade.getNumBytesAsUTF8());

    std::string responseHeader;
    while (responseHeader.find ("\r\n\r\n") == std::string::npos)
    {
        char c;
        readExact (&c, 1);
        responseHeader += c;
        if (responseHeader.size() > 16384)
            throw ObsError (ObsError::Unreachable, notObsMessage);
    }
    if (responseHeader.rfind ("HTTP/1.1 101", 0) != 0)
        throw ObsError (ObsError::Unreachable, notObsMessage);

    // Hello -> Identify -> Identified
    auto hello = receive();
    if ((int) hello["op"] != 0)
        throw ObsError (ObsError::Closed, "Unexpected reply from OBS.");

    auto* identify = new juce::DynamicObject();
    identify->setProperty ("rpcVersion", 1);
    identify->setProperty ("eventSubscriptions", 0);
    if (auto auth = hello["d"]["authentication"]; auth.isObject())
    {
        if (password.isEmpty())
            throw ObsError (ObsError::NeedPassword, needPasswordMessage);
        auto secret = sha256Base64 (password + auth["salt"].toString());
        identify->setProperty ("authentication", sha256Base64 (secret + auth["challenge"].toString()));
    }
    sendText (juce::JSON::toString (J ({ { "op", 1 }, { "d", juce::var (identify) } }), true));

    if ((int) receive()["op"] != 2)
        throw ObsError (ObsError::BadPassword, badPasswordMessage);
}

ObsClient::~ObsClient()
{
    if (! socket.isConnected())
        return;
    try
    {
        const uint8_t normalClosure[2] = { 0x03, 0xE8 };   // 1000
        sendFrame (opClose, normalClosure, sizeof (normalClosure));
    }
    catch (const ObsError&) {}
    socket.close();
}

juce::var ObsClient::request (const juce::String& type, const juce::var& data)
{
    const auto id = juce::String (++nextRequestId);
    auto* payload = new juce::DynamicObject();
    payload->setProperty ("requestType", type);
    payload->setProperty ("requestId", id);
    if (! data.isVoid())
        payload->setProperty ("requestData", data);
    sendText (juce::JSON::toString (J ({ { "op", 6 }, { "d", juce::var (payload) } }), true));

    for (;;)
    {
        auto message = receive();
        auto response = message["d"];
        if ((int) message["op"] != 7 || response["requestId"].toString() != id)
            continue;

        auto status = response["requestStatus"];
        if (! (bool) status["result"])
            throw ObsError (ObsError::Request, type + " failed: " + status["comment"].toString()
                                                   + " (code " + status["code"].toString() + ")");
        return response["responseData"];
    }
}

void ObsClient::writeAll (const void* data, size_t numBytes)
{
    auto* p = static_cast<const char*> (data);
    while (numBytes > 0)
    {
        const int written = socket.write (p, (int) numBytes);
        if (written <= 0)
            throw ObsError (ObsError::Closed, "Lost the connection to OBS.");
        p += written;
        numBytes -= (size_t) written;
    }
}

void ObsClient::readExact (void* dest, size_t numBytes)
{
    auto* p = static_cast<char*> (dest);
    const auto deadline = juce::Time::currentTimeMillis() + timeoutMs;
    while (numBytes > 0)
    {
        const auto remaining = deadline - juce::Time::currentTimeMillis();
        if (remaining <= 0)
            throw ObsError (ObsError::Timeout, timeoutMessage);

        const int ready = socket.waitUntilReady (true, (int) remaining);
        if (ready < 0)
            throw ObsError (ObsError::Closed, "Lost the connection to OBS.");
        if (ready == 0)
            continue;

        const int received = socket.read (p, (int) numBytes, false);
        if (received <= 0)
            throw ObsError (ObsError::Closed, "OBS closed the connection.");
        p += received;
        numBytes -= (size_t) received;
    }
}

void ObsClient::sendFrame (int opcode, const void* data, size_t numBytes)
{
    uint8_t header[14];
    size_t headerSize = 0;
    header[headerSize++] = (uint8_t) (0x80 | opcode);                 // FIN + opcode

    if (numBytes < 126)
    {
        header[headerSize++] = (uint8_t) (0x80 | numBytes);           // MASK bit + length
    }
    else if (numBytes < 65536)
    {
        header[headerSize++] = 0x80 | 126;
        header[headerSize++] = (uint8_t) (numBytes >> 8);
        header[headerSize++] = (uint8_t) numBytes;
    }
    else
    {
        header[headerSize++] = 0x80 | 127;
        for (int shift = 56; shift >= 0; shift -= 8)
            header[headerSize++] = (uint8_t) ((uint64_t) numBytes >> shift);
    }

    uint8_t mask[4];                                                   // client frames must be masked
    juce::Random::getSystemRandom().fillBitsRandomly (mask, sizeof (mask));
    for (auto b : mask)
        header[headerSize++] = b;

    juce::MemoryBlock frame (header, headerSize);
    frame.setSize (headerSize + numBytes);
    auto* src = static_cast<const uint8_t*> (data);
    auto* dst = static_cast<uint8_t*> (frame.getData()) + headerSize;
    for (size_t i = 0; i < numBytes; ++i)
        dst[i] = src[i] ^ mask[i & 3];

    writeAll (frame.getData(), frame.getSize());
}

void ObsClient::sendText (const juce::String& text)
{
    sendFrame (opText, text.toRawUTF8(), text.getNumBytesAsUTF8());
}

juce::var ObsClient::receive()
{
    juce::MemoryBlock message;
    for (;;)
    {
        uint8_t header[2];
        readExact (header, 2);
        const bool isFinal = (header[0] & 0x80) != 0;
        const int opcode = header[0] & 0x0F;
        const bool isMasked = (header[1] & 0x80) != 0;

        uint64_t length = header[1] & 0x7F;
        if (length == 126 || length == 127)
        {
            uint8_t extended[8];
            const size_t n = length == 126 ? 2 : 8;
            readExact (extended, n);
            length = 0;
            for (size_t i = 0; i < n; ++i)
                length = (length << 8) | extended[i];
        }
        if (length > maxMessageSize)
            throw ObsError (ObsError::Closed, "Message from OBS too large.");

        uint8_t mask[4] = {};
        if (isMasked)
            readExact (mask, 4);

        juce::MemoryBlock payload ((size_t) length);
        if (length > 0)
            readExact (payload.getData(), (size_t) length);
        auto* bytes = static_cast<uint8_t*> (payload.getData());
        if (isMasked)
            for (uint64_t i = 0; i < length; ++i)
                bytes[i] ^= mask[i & 3];

        if (opcode == opClose)
        {
            const int code = length >= 2 ? (bytes[0] << 8) | bytes[1] : 0;
            socket.close();
            if (code == closeAuthenticationFailed)
                throw ObsError (ObsError::BadPassword, badPasswordMessage);
            throw ObsError (ObsError::Closed, code != 0 ? "OBS closed the connection (code " + juce::String (code) + ")."
                                                        : juce::String ("OBS closed the connection."));
        }
        if (opcode == opPing)
        {
            sendFrame (opPong, payload.getData(), (size_t) length);
            continue;
        }
        if (opcode == opPong)
            continue;

        message.append (payload.getData(), (size_t) length);
        if (! isFinal)
            continue;

        auto parsed = juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (message.getData()),
                                                                 (int) message.getSize()));
        message.reset();
        if (parsed.isObject())
            return parsed;
    }
}
} // namespace pc
