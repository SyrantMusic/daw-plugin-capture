// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 DAW Plugin Capture contributors
#include "TerminalLookAndFeel.h"

namespace term
{
using namespace colours;

static juce::String monospaceFamily()
{
    const auto installed = juce::Font::findAllTypefaceNames();
    for (auto* family : { "Cascadia Mono", "Cascadia Code", "Consolas", "Lucida Console", "Courier New" })
        if (installed.contains (family))
            return family;
    return juce::Font::getDefaultMonospacedFontName();
}

juce::Font font (bool bold)
{
    static const juce::String family = monospaceFamily();
    return juce::Font (juce::FontOptions (family, 14.0f, bold ? juce::Font::bold : juce::Font::plain));
}

int textWidth (const juce::String& text, bool bold)
{
    return juce::GlyphArrangement::getStringWidthInt (font (bold), text);
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    using namespace juce;
    setColour (ResizableWindow::backgroundColourId, background);
    setColour (TextEditor::backgroundColourId, panel);
    setColour (TextEditor::textColourId, yellow);
    setColour (TextEditor::outlineColourId, track);
    setColour (TextEditor::focusedOutlineColourId, cyan);
    setColour (TextEditor::highlightColourId, track);
    setColour (TextEditor::highlightedTextColourId, yellow);
    setColour (CaretComponent::caretColourId, yellow);
    setColour (ComboBox::backgroundColourId, panel);
    setColour (ComboBox::textColourId, yellow);
    setColour (ComboBox::outlineColourId, track);
    setColour (ComboBox::focusedOutlineColourId, cyan);
    setColour (ComboBox::arrowColourId, cyan);
    setColour (PopupMenu::backgroundColourId, panel);
    setColour (PopupMenu::textColourId, cyan);
    setColour (PopupMenu::highlightedBackgroundColourId, track);
    setColour (PopupMenu::highlightedTextColourId, cyan);
    setColour (Label::textColourId, foreground);
    setColour (Label::textWhenEditingColourId, yellow);
    setColour (Label::backgroundWhenEditingColourId, panel);
    setColour (Label::outlineWhenEditingColourId, Colours::transparentBlack);
    setColour (ToggleButton::textColourId, dim);
    setColour (ToggleButton::tickColourId, cyan);
    setColour (ToggleButton::tickDisabledColourId, track);
    setColour (ScrollBar::thumbColourId, track);
    setColour (TextButton::buttonColourId, panel);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                        bool isOver, bool isDown)
{
    g.setColour ((isOver || isDown) && button.isEnabled() ? track : panel);
    g.fillRect (button.getLocalBounds());
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (font (true));
    g.setColour (button.isEnabled() ? button.findColour (juce::TextButton::textColourOffId) : track);
    g.drawText (button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
}

void LookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool isOver, bool)
{
    const auto bounds = button.getLocalBounds();
    const auto box = juce::Rectangle<float> (0.0f, (float) (bounds.getHeight() - 13) * 0.5f, 13.0f, 13.0f);
    g.setColour (panel);
    g.fillRect (box);
    g.setColour (isOver ? dim : track);
    g.drawRect (box, 1.0f);

    if (button.getToggleState())
    {
        auto tick = getTickShape (1.0f);
        g.setColour (button.isEnabled() ? cyan : track);
        g.fillPath (tick, tick.getTransformToScaleToFit (box.reduced (2.5f), true));
    }

    g.setFont (font());
    g.setColour (isOver && button.isEnabled() ? foreground : dim);
    g.drawText (button.getButtonText(), bounds.withTrimmedLeft (20), juce::Justification::centredLeft, false);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
{
    g.setColour (panel);
    g.fillRect (0, 0, width, height);
    g.setColour (box.hasKeyboardFocus (true) ? cyan : track);
    g.drawRect (0, 0, width, height, 1);

    juce::Path arrow;
    const float cx = (float) width - 12.0f, cy = (float) height * 0.5f;
    arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
    g.setColour (box.isEnabled() ? cyan : track);
    g.fillPath (arrow);
}

void LookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (1, 1, box.getWidth() - 24, box.getHeight() - 2);
    label.setFont (font());
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (panel);
    g.setColour (track);
    g.drawRect (0, 0, width, height, 1);
}

void LookAndFeel::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height,
                                 bool isVertical, int thumbStart, int thumbSize, bool isOver, bool isDown)
{
    if (thumbSize <= 0)
        return;

    const auto thumb = isVertical
        ? juce::Rectangle<float> ((float) x + 2, (float) thumbStart + 2, (float) width - 4, (float) thumbSize - 4)
        : juce::Rectangle<float> ((float) thumbStart, (float) y + 2, (float) thumbSize, (float) height - 4);
    g.setColour (isDown ? cyan : (isOver ? dim : track));
    g.fillRoundedRectangle (thumb, 3.0f);
}

//==============================================================================
LogView::LogView()
{
    setViewedComponent (&content, false);
    setScrollBarsShown (true, false);
    setScrollBarThickness (10);
}

void LogView::append (const juce::String& line, juce::Colour colour)
{
    lines.emplace_back (line, colour);
    while (lines.size() > 1000)
        lines.pop_front();
    relayout (false);
}

void LogView::resized()
{
    juce::Viewport::resized();
    relayout (true);
}

void LogView::relayout (bool scrollToEnd)
{
    const bool wasAtEnd = getViewPositionY() + getViewHeight() >= content.getHeight() - 6;

    juce::AttributedString attributed;
    attributed.setWordWrap (juce::AttributedString::byWord);
    for (auto& [line, colour] : lines)
        attributed.append (line + "\n", font(), colour);

    const int width = std::max (50, getMaximumVisibleWidth() - 4);
    content.layout.createLayout (attributed, (float) width);
    content.setSize (width, std::max (getHeight(), (int) std::ceil (content.layout.getHeight()) + 4));
    content.repaint();                      // setSize only repaints when the size changes

    if (wasAtEnd || scrollToEnd)
        setViewPosition (0, std::max (0, content.getHeight() - getViewHeight()));
}

//==============================================================================
void Tab::paint (juce::Graphics& g)
{
    g.fillAll (active ? activeColour : panel);
    g.setFont (font (true));
    g.setColour (active ? background : dim);
    g.drawText (text, getLocalBounds(), juce::Justification::centred, false);
}
} // namespace term
