// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 DAW Plugin Capture contributors
#include "PluginProcessor.h"
#include "PluginEditor.h"

pc::Config toConfig (const pc::Settings& settings)
{
    pc::Config config;
    config.host = settings.host.trim().isEmpty() ? juce::String ("localhost") : settings.host.trim();
    config.port = settings.port;
    config.password = settings.password;
    config.scene = settings.scene.trim();
    config.ignore = settings.ignore;
    return config;
}

Processor::Processor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      engine (pc::Engine::acquire())
{
    // --start-on-load waits briefly, so hosts that load plugins momentarily (scanning, previews)
    // don't start a capture.
    startTimer (1500);
}

Processor::~Processor()
{
    stopTimer();   // the last instance releases the engine, which removes its sources and restores windows
}

void Processor::timerCallback()
{
    stopTimer();
    if (engine->autostartHandled)
        return;
    engine->autostartHandled = true;

    const auto settings = pc::loadSettings();
    if (settings.autostart && settings.scene.isNotEmpty() && ! engine->isRunning())
    {
        engine->log ("PS> capture --scene \"" + settings.scene + "\"   (--start-on-load)", pc::Tag::Green);
        engine->start (toConfig (settings));
    }
}

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    if (output != juce::AudioChannelSet::mono() && output != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == output;
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // Audio is processed in place, so the input already is the output.
    for (int channel = getTotalNumInputChannels(); channel < getTotalNumOutputChannels(); ++channel)
        buffer.clear (channel, 0, buffer.getNumSamples());
}

juce::AudioProcessorEditor* Processor::createEditor()
{
    return new Editor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new Processor();
}
