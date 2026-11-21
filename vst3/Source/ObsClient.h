// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 DAW Plugin Capture contributors
#pragma once

#include <juce_core/juce_core.h>
#include <stdexcept>

namespace pc
{
/** An error talking to OBS, with a message suitable for showing to the user. */
struct ObsError : std::runtime_error
{
    enum Kind { Unreachable, NeedPassword, BadPassword, Timeout, Closed, Request };

    ObsError (Kind k, const juce::String& message) : std::runtime_error (message.toStdString()), kind (k) {}

    /** True for authentication problems, which retrying won't fix. */
    bool isFatal() const noexcept         { return kind == NeedPassword || kind == BadPassword; }
    juce::String message() const          { return juce::String::fromUTF8 (what()); }

    Kind kind;
};

/** Minimal blocking obs-websocket 5 client (WebSocket framing, SHA-256 authentication, JSON). */
class ObsClient
{
public:
    ObsClient (const juce::String& host, int port, const juce::String& password, int timeoutMs);
    ~ObsClient();

    /** Sends a request and returns its responseData. Throws ObsError on failure. */
    juce::var request (const juce::String& type, const juce::var& data = {});

private:
    void writeAll (const void* data, size_t numBytes);
    void readExact (void* dest, size_t numBytes);
    void sendFrame (int opcode, const void* data, size_t numBytes);
    void sendText (const juce::String& text);
    juce::var receive();

    juce::StreamingSocket socket;
    int timeoutMs;
    int nextRequestId = 0;

    JUCE_DECLARE_NON_COPYABLE (ObsClient)
};

/** Builds a JSON object: J ({ { "key", value }, ... }). */
juce::var J (std::initializer_list<std::pair<juce::Identifier, juce::var>> properties);
} // namespace pc
