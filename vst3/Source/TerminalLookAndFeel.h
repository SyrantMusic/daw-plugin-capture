// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 DAW Plugin Capture contributors
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <deque>

/** Terminal-style theme shared with the desktop app. */
namespace term
{
namespace colours
{
    const juce::Colour background { 0xff0c0c0c }, panel { 0xff161616 }, foreground { 0xffcccccc }, dim { 0xff6a6a6a },
                       cyan { 0xff3ad7e6 }, green { 0xff4ec95b }, yellow { 0xffe5c07b }, red { 0xfff05d5d },
                       magenta { 0xfff92672 }, track { 0xff3a3a3a };
}

/** The first installed of Cascadia Mono, Consolas, ... at the app's size. */
juce::Font font (bool bold = false);
int textWidth (const juce::String& text, bool bold = false);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    juce::Font getTextButtonFont (juce::TextButton&, int) override   { return font (true); }
    juce::Font getComboBoxFont (juce::ComboBox&) override            { return font(); }
    juce::Font getPopupMenuFont() override                           { return font(); }
    juce::Font getLabelFont (juce::Label& label) override            { return label.getFont(); }
    int getDefaultScrollbarWidth() override                          { return 10; }

    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool isOver, bool isDown) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool isOver, bool isDown) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool isOver, bool isDown) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isVertical,
                        int thumbStart, int thumbSize, bool isOver, bool isDown) override;
};

/** Read-only, word-wrapped log with a colour per line.
    (TextEditor recolours all of its text whenever its text colour changes.) */
class LogView : public juce::Viewport
{
public:
    LogView();
    void append (const juce::String& text, juce::Colour colour);
    void resized() override;

private:
    struct Content : juce::Component
    {
        juce::TextLayout layout;
        void paint (juce::Graphics& g) override   { layout.draw (g, getLocalBounds().toFloat()); }
    };

    void relayout (bool scrollToEnd);

    Content content;
    std::deque<std::pair<juce::String, juce::Colour>> lines;
};

/** A tab-bar button: highlighted in its colour when active. */
struct Tab : juce::Component
{
    juce::String text;
    juce::Colour activeColour;
    bool active = false;
    std::function<void()> onClick;

    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override   { if (contains (e.getPosition()) && onClick) onClick(); }
};

/** A container whose background is drawn by a callback. */
struct Page : juce::Component
{
    std::function<void (juce::Graphics&)> painter;
    void paint (juce::Graphics& g) override             { if (painter) painter (g); }
};
} // namespace term
