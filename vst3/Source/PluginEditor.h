// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 DAW Plugin Capture contributors
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "TerminalLookAndFeel.h"

/** The CAPTURE / CONNECTION UI, matching the desktop app. Polls the shared engine at 10 Hz. */
class Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit Editor (Processor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    void timerCallback() override;
    void layoutCapturePage (int width, int height);
    void layoutConnectionPage (int width, int height);

    void showTab (int index);
    pc::Settings currentSettings() const;
    void save();
    void startCapture();
    void updateTarget();
    void setBusy (bool);
    void showConnectionMessage (const juce::String&, juce::Colour);

    void styleLabel (juce::Label&, const juce::String&, juce::Colour, bool bold = false);
    void styleEntry (juce::TextEditor&);
    void styleButton (juce::TextButton&, const juce::String&, juce::Colour, std::function<void()>);
    void styleFlag (juce::ToggleButton&, const juce::String&, std::function<void()> = {});

    pc::Engine& engine;
    term::LookAndFeel lookAndFeel;

    term::Tab captureTab, connectionTab;
    juce::Label status;
    term::Page capturePage, connectionPage;

    // 1 CAPTURE
    juce::Label prompt, sceneLabel, target, windowsHeader;
    juce::ComboBox sceneBox;
    juce::TextButton refreshButton, startButton, stopButton;
    juce::ToggleButton autostartToggle;
    term::LogView logView;
    juce::Rectangle<int> windowsPanel;
    juce::StringArray windowTitles;

    // 2 CONNECTION
    juce::Label websocketHeader, privacyHeader, hostLabel, portLabel, passwordLabel, ignoreLabel;
    juce::Label hostHint, portHint, passwordHint, savePasswordHint, ignoreHint, connectionStatus, help, legal;
    juce::TextEditor hostEditor, portEditor, passwordEditor, ignoreEditor;
    juce::ToggleButton showPassword, savePassword;
    juce::TextButton testButton, saveButton;
    int dividerY = 0;

    int nextLogLine = 0, scenesGeneration = 0, testGeneration = 0;
    bool busy = false;
    pc::Status shownStatus = pc::Status::Stopped;
    void* ownWindow = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Editor)
};
