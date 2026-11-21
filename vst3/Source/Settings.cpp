// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 DAW Plugin Capture contributors
#include "Settings.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>

namespace pc
{
juce::File settingsFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("DAW Plugin Capture")
        .getChildFile ("settings.json");
}

juce::String protect (const juce::String& text)
{
    if (text.isEmpty())
        return {};

    auto utf8 = text.toStdString();
    DATA_BLOB in { (DWORD) utf8.size(), reinterpret_cast<BYTE*> (utf8.data()) }, out {};
    if (! CryptProtectData (&in, L"DAW Plugin Capture", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};

    auto result = juce::Base64::toBase64 (out.pbData, out.cbData);
    LocalFree (out.pbData);
    return result;
}

juce::String unprotect (const juce::String& base64)
{
    if (base64.isEmpty())
        return {};

    juce::MemoryOutputStream encrypted;
    if (! juce::Base64::convertFromBase64 (encrypted, base64))
        return {};

    DATA_BLOB in { (DWORD) encrypted.getDataSize(), (BYTE*) encrypted.getData() }, out {};
    if (! CryptUnprotectData (&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return {};

    auto result = juce::String::fromUTF8 (reinterpret_cast<const char*> (out.pbData), (int) out.cbData);
    SecureZeroMemory (out.pbData, out.cbData);
    LocalFree (out.pbData);
    return result;
}

static juce::var readSettingsJson()
{
    auto file = settingsFile();
    if (! file.existsAsFile())
        return {};
    auto json = juce::JSON::parse (file);
    return json.isObject() ? json : juce::var();
}

Settings loadSettings()
{
    Settings s;
    auto json = readSettingsJson();
    if (! json.isObject())
        return s;

    auto has = [&json] (const char* key) { return json.hasProperty (key); };
    if (has ("host"))          s.host = json["host"].toString();
    if (has ("scene"))         s.scene = json["scene"].toString();
    if (has ("ignore"))        s.ignore = json["ignore"].toString();
    if (has ("port"))          s.port = (int) json["port"];
    if (has ("save_password")) s.savePassword = (bool) json["save_password"];
    if (has ("autostart"))     s.autostart = (bool) json["autostart"];
    s.password = unprotect (json["password_dpapi"].toString());
    return s;
}

void saveSettings (const Settings& s)
{
    auto json = readSettingsJson();
    if (! json.isObject())
        json = juce::var (new juce::DynamicObject());

    auto* object = json.getDynamicObject();
    object->setProperty ("host", s.host);
    object->setProperty ("port", s.port);
    object->setProperty ("scene", s.scene);
    object->setProperty ("ignore", s.ignore);
    object->setProperty ("save_password", s.savePassword);
    object->setProperty ("autostart", s.autostart);
    object->setProperty ("password_dpapi", protect (s.savePassword ? s.password : juce::String()));
    object->removeProperty ("password");

    auto file = settingsFile();
    file.getParentDirectory().createDirectory();
    file.replaceWithText (juce::JSON::toString (json, false));     // written to a temp file, then swapped in
}
} // namespace pc
