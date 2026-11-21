// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 DAW Plugin Capture contributors
#pragma once

#include <juce_core/juce_core.h>

namespace pc
{
/** Title words of windows that are never captured (licence, activation, login and settings windows). */
inline constexpr const char* defaultIgnore =
    "authoriz, activat, licen, login, log in, sign in, sign-in, register, serial, account, password, "
    "settings, preferences";

/** Settings shared with the desktop app, stored in %APPDATA%\DAW Plugin Capture\settings.json. */
struct Settings
{
    juce::String host = "localhost";
    int port = 4455;
    juce::String password;
    juce::String scene;
    juce::String ignore = defaultIgnore;
    bool savePassword = true;
    bool autostart = false;
};

juce::File settingsFile();
Settings loadSettings();

/** Saves settings, keeping keys only the desktop app uses. The password is DPAPI-encrypted. */
void saveSettings (const Settings&);

/** Encrypts text for the current Windows user (DPAPI) and returns base64; empty in, empty out. */
juce::String protect (const juce::String& text);

/** Reverses protect(); returns an empty string if the value can't be decrypted. */
juce::String unprotect (const juce::String& base64);
} // namespace pc
