// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 DAW Plugin Capture contributors
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "CaptureEngine.h"
#include "Settings.h"

/**
    Pass-through effect that hosts the capture engine inside the DAW.

    Settings live in %APPDATA% (shared with the desktop app) rather than in the plugin state, so a
    project file can be shared without leaking the OBS password.
*/
class Processor : public juce::AudioProcessor, private juce::Timer
{
public:
    Processor();
    ~Processor() override;

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                    { return true; }

    const juce::String getName() const override        { return JucePlugin_Name; }
    bool acceptsMidi() const override                  { return false; }
    bool producesMidi() const override                 { return false; }
    bool isMidiEffect() const override                 { return false; }
    double getTailLengthSeconds() const override       { return 0.0; }

    int getNumPrograms() override                      { return 1; }
    int getCurrentProgram() override                   { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override   { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    pc::Engine& getEngine() noexcept                   { return *engine; }

private:
    void timerCallback() override;

    std::shared_ptr<pc::Engine> engine;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Processor)
};

pc::Config toConfig (const pc::Settings&);
