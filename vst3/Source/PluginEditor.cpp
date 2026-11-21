// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 DAW Plugin Capture contributors
#include "PluginEditor.h"

using namespace term::colours;
using term::font;
using term::textWidth;

namespace
{
juce::String utf8 (const char* text)
{
    return juce::String::fromUTF8 (text);
}

struct StatusStyle
{
    const char* text;
    juce::Colour colour;
};

// Indexed by pc::Status
const StatusStyle statusStyles[] = {
    { "\xe2\x97\x8b idle", dim },
    { "\xe2\x97\x8c connecting\xe2\x80\xa6", yellow },
    { "\xe2\x97\x8f capturing", green },
    { "\xe2\x97\x8c waiting for OBS", yellow },
};

juce::Colour colourFor (pc::Tag tag)
{
    switch (tag)
    {
        case pc::Tag::Dim:     return dim;
        case pc::Tag::Cyan:    return cyan;
        case pc::Tag::Green:   return green;
        case pc::Tag::Yellow:  return yellow;
        case pc::Tag::Red:     return red;
        case pc::Tag::Magenta: return magenta;
        case pc::Tag::Normal:  break;
    }
    return foreground;
}

juce::String legalNotice()
{
    juce::String notice = juce::String (JucePlugin_Name) + " " + JucePlugin_VersionString
                        + utf8 ("  \xc2\xb7  Copyright (C) 2026 DAW Plugin Capture contributors\n")
                        + "Free software under the GNU AGPL v3 or later, provided WITHOUT ANY WARRANTY. Built with JUCE.";
    if (juce::String url (DAW_PLUGIN_CAPTURE_URL); url.isNotEmpty())
        notice << "\nSource code: " << url;
    return notice;
}

const juce::juce_wchar passwordBullet = 0x2022;
} // namespace

//==============================================================================
void Editor::styleLabel (juce::Label& label, const juce::String& labelText, juce::Colour colour, bool bold)
{
    label.setText (labelText, juce::dontSendNotification);
    label.setFont (font (bold));
    label.setColour (juce::Label::textColourId, colour);
    label.setBorderSize ({});
    label.setMinimumHorizontalScale (1.0f);
    label.setJustificationType (juce::Justification::centredLeft);
}

void Editor::styleEntry (juce::TextEditor& editor)
{
    // Text keeps the colour it was inserted with, so set colours before any text goes in.
    editor.setColour (juce::TextEditor::textColourId, yellow);
    editor.setColour (juce::TextEditor::backgroundColourId, panel);
    editor.setColour (juce::TextEditor::outlineColourId, track);
    editor.setColour (juce::TextEditor::focusedOutlineColourId, cyan);
    editor.setColour (juce::CaretComponent::caretColourId, yellow);
    editor.setFont (font());
    editor.setJustification (juce::Justification::centredLeft);
    editor.setIndents (6, 0);
}

void Editor::styleButton (juce::TextButton& button, const juce::String& label, juce::Colour colour,
                          std::function<void()> onClick)
{
    button.setButtonText (label);
    button.setColour (juce::TextButton::textColourOffId, colour);
    button.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    button.onClick = std::move (onClick);
}

void Editor::styleFlag (juce::ToggleButton& toggle, const juce::String& label, std::function<void()> onClick)
{
    toggle.setButtonText (label);
    toggle.onClick = std::move (onClick);
}

//==============================================================================
Editor::Editor (Processor& processor)
    : AudioProcessorEditor (processor), engine (processor.getEngine())
{
    setLookAndFeel (&lookAndFeel);
    const auto settings = pc::loadSettings();

    // Tab bar
    captureTab.text = "  1  CAPTURE  ";
    captureTab.activeColour = green;
    captureTab.onClick = [this] { showTab (0); };
    connectionTab.text = "  2  CONNECTION  ";
    connectionTab.activeColour = cyan;
    connectionTab.onClick = [this] { showTab (1); };
    for (auto* tab : { &captureTab, &connectionTab })
    {
        tab->setMouseCursor (juce::MouseCursor::PointingHandCursor);
        addAndMakeVisible (tab);
    }
    styleLabel (status, {}, dim, true);
    addAndMakeVisible (status);
    addChildComponent (capturePage);
    addChildComponent (connectionPage);

    // 1 CAPTURE
    styleLabel (prompt, "PS>", green, true);
    styleLabel (sceneLabel, " OBS scene:", foreground);
    sceneBox.setEditableText (true);
    sceneBox.setText (settings.scene, juce::dontSendNotification);
    styleButton (refreshButton, utf8 ("\xe2\x86\xbb Scenes"), cyan,
                 [this] { engine.refreshScenes (toConfig (currentSettings()), false); });
    styleButton (startButton, utf8 ("\xe2\x96\xb6 Start"), green, [this] { startCapture(); });
    styleButton (stopButton, utf8 ("\xe2\x96\xa0 Stop"), red, [this]
    {
        engine.stop();
        stopButton.setEnabled (false);
    });
    styleFlag (autostartToggle, "--start-on-load", [this] { save(); });
    autostartToggle.setToggleState (settings.autostart, juce::dontSendNotification);
    styleLabel (target, {}, dim);
    styleLabel (windowsHeader, "CAPTURED WINDOWS", dim, true);

    for (auto* c : std::initializer_list<juce::Component*> { &prompt, &sceneLabel, &sceneBox, &refreshButton, &startButton,
                                                            &stopButton, &autostartToggle, &target, &logView, &windowsHeader })
        capturePage.addAndMakeVisible (c);

    capturePage.painter = [this] (juce::Graphics& g)
    {
        g.setColour (panel);
        g.fillRect (windowsPanel);

        juce::AttributedString list;
        list.setWordWrap (juce::AttributedString::byWord);
        if (windowTitles.isEmpty())
            list.append (utf8 ("\xc2\xb7"), font(), track);
        for (auto& title : windowTitles)
            list.append (utf8 ("\xe2\x96\xb8 ") + title + "\n", font(), cyan);

        const auto area = windowsPanel.reduced (10, 8).withTrimmedTop (24).toFloat();
        juce::TextLayout layout;
        layout.createLayout (list, area.getWidth());
        g.reduceClipRegion (area.toNearestInt());
        layout.draw (g, area.withHeight (std::max (area.getHeight(), layout.getHeight())));
    };

    // 2 CONNECTION
    styleLabel (websocketHeader, "OBS WEBSOCKET", cyan, true);
    styleLabel (hostLabel, "--host", foreground);
    styleLabel (portLabel, "--port", foreground);
    styleLabel (passwordLabel, "--password", foreground);
    styleLabel (privacyHeader, "PRIVACY", cyan, true);
    styleLabel (ignoreLabel, "--ignore", foreground);
    styleLabel (hostHint, "localhost if OBS runs on this PC", dim);
    styleLabel (portHint, "OBS default: 4455", dim);
    styleLabel (passwordHint, "blank if authentication is off", dim);
    styleLabel (savePasswordHint, "encrypted for your Windows account only", dim);
    styleLabel (ignoreHint, "windows with these title words are never shown", dim);
    styleLabel (connectionStatus, {}, dim);
    styleLabel (help, utf8 ("In OBS:  Tools > WebSocket Server Settings > tick 'Enable WebSocket server'.\n"
                            "         'Show Connect Info' shows the port and password to copy here.\n\n"
                            "Your OBS canvas is assumed to show one full monitor (e.g. a 1920x1080 canvas for a 1080p screen)\n"
                            "so plugin windows land where they are on screen - put your DAW's Window Capture full-canvas.\n\n"
                            "Settings are shared with the DAW Plugin Capture app. Changes apply the next time you press \xe2\x96\xb6 Start."),
                dim);
    help.setJustificationType (juce::Justification::topLeft);
    styleLabel (legal, legalNotice(), track);
    legal.setJustificationType (juce::Justification::bottomLeft);

    for (auto* editor : { &hostEditor, &portEditor, &passwordEditor, &ignoreEditor })
        styleEntry (*editor);
    hostEditor.setText (settings.host, false);
    portEditor.setText (juce::String (settings.port), false);
    portEditor.setInputRestrictions (5, "0123456789");
    passwordEditor.setText (settings.password, false);
    passwordEditor.setPasswordCharacter (passwordBullet);
    ignoreEditor.setText (settings.ignore, false);
    for (auto* editor : { &hostEditor, &portEditor, &passwordEditor, &ignoreEditor })
        editor->setCaretPosition (0);
    hostEditor.onTextChange = portEditor.onTextChange = [this] { updateTarget(); };

    styleFlag (showPassword, "show", [this]
    {
        passwordEditor.setPasswordCharacter (showPassword.getToggleState() ? 0 : passwordBullet);
    });
    styleFlag (savePassword, "--save-password", [this] { save(); });
    savePassword.setToggleState (settings.savePassword, juce::dontSendNotification);
    styleButton (testButton, "Test connection", cyan, [this]
    {
        showConnectionMessage (utf8 ("connecting\xe2\x80\xa6"), yellow);
        testButton.setEnabled (false);
        engine.testConnection (toConfig (currentSettings()));
    });
    styleButton (saveButton, "Save", green, [this]
    {
        save();
        showConnectionMessage ("Saved.", green);
    });

    for (auto* c : std::initializer_list<juce::Component*> {
             &websocketHeader, &hostLabel, &portLabel, &passwordLabel, &privacyHeader, &ignoreLabel, &hostHint,
             &portHint, &passwordHint, &savePasswordHint, &ignoreHint, &connectionStatus, &help, &legal, &hostEditor,
             &portEditor, &passwordEditor, &ignoreEditor, &showPassword, &savePassword, &testButton, &saveButton })
        connectionPage.addAndMakeVisible (c);

    connectionPage.painter = [this] (juce::Graphics& g)
    {
        g.setColour (track);
        g.fillRect (12, dividerY, connectionPage.getWidth() - 24, 1);
    };

    // Initial state
    if (engine.logCount() == 0)
    {
        engine.log ("DAW Plugin Capture - shows this DAW's plugin windows on stream, without capturing the whole screen.",
                    pc::Tag::Dim);
        engine.log (settings.scene.isEmpty()
                        ? utf8 ("First run: set up the OBS connection on the CONNECTION tab, then pick a scene here and press \xe2\x96\xb6 Start.")
                        : utf8 ("Pick the OBS scene that holds your DAW capture and press \xe2\x96\xb6 Start. Open a plugin and it appears "
                                "in OBS; close it and it's removed."),
                    settings.scene.isEmpty() ? pc::Tag::Yellow : pc::Tag::Dim);
    }
    scenesGeneration = engine.scenes().generation;
    testGeneration = engine.testResult().generation;
    busy = ! engine.isRunning();
    setBusy (engine.isRunning());
    updateTarget();
    timerCallback();
    if (settings.scene.isNotEmpty() && ! engine.isRunning())
        engine.refreshScenes (toConfig (settings), true);

    showTab (settings.scene.isEmpty() ? 1 : 0);
    setResizable (true, true);
    setResizeLimits (860, 460, 2600, 1600);
    setSize (1100, 600);
    setWantsKeyboardFocus (true);
    startTimerHz (10);
}

Editor::~Editor()
{
    stopTimer();
    engine.setOwnWindow (ownWindow, false);
    save();
    setLookAndFeel (nullptr);
}

//==============================================================================
pc::Settings Editor::currentSettings() const
{
    pc::Settings s;
    s.host = hostEditor.getText().trim();
    if (s.host.isEmpty())
        s.host = "localhost";
    s.port = portEditor.getText().trim().getIntValue();
    if (s.port <= 0)
        s.port = 4455;
    s.password = passwordEditor.getText();
    s.scene = sceneBox.getText().trim();
    s.ignore = ignoreEditor.getText();
    s.savePassword = savePassword.getToggleState();
    s.autostart = autostartToggle.getToggleState();
    return s;
}

void Editor::save()
{
    pc::saveSettings (currentSettings());
}

void Editor::updateTarget()
{
    const auto host = hostEditor.getText().trim();
    target.setText (utf8 ("\xe2\x94\x82  ") + (host.isEmpty() ? juce::String ("localhost") : host) + ":" + portEditor.getText()
                        + utf8 ("  \xc2\xb7  plugin windows of ") + pc::hostExeName() + "  (change on the CONNECTION tab)",
                    juce::dontSendNotification);
}

void Editor::showConnectionMessage (const juce::String& message, juce::Colour colour)
{
    connectionStatus.setText (message, juce::dontSendNotification);
    connectionStatus.setColour (juce::Label::textColourId, colour);
}

void Editor::setBusy (bool isBusy)
{
    if (isBusy == busy)
        return;
    busy = isBusy;
    startButton.setEnabled (! isBusy);
    stopButton.setEnabled (isBusy);
    refreshButton.setEnabled (! isBusy);
    sceneBox.setEnabled (! isBusy);
}

void Editor::startCapture()
{
    if (engine.isRunning())
        return;

    const auto settings = currentSettings();
    if (settings.scene.isEmpty())
    {
        engine.log (utf8 ("Pick an OBS scene first (\xe2\x86\xbb Scenes loads the list from OBS)."), pc::Tag::Red);
        return;
    }
    save();
    engine.log ({});
    engine.log ("PS> capture --scene \"" + settings.scene + "\"", pc::Tag::Green);
    engine.start (toConfig (settings));
    setBusy (true);
}

void Editor::showTab (int index)
{
    captureTab.active = index == 0;
    connectionTab.active = index == 1;
    captureTab.repaint();
    connectionTab.repaint();
    capturePage.setVisible (index == 0);
    connectionPage.setVisible (index == 1);
}

bool Editor::keyPressed (const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() && (key.getKeyCode() == '1' || key.getKeyCode() == '2'))
    {
        showTab (key.getKeyCode() == '1' ? 0 : 1);
        return true;
    }
    return false;
}

//==============================================================================
void Editor::timerCallback()
{
    // Keep the window hosting this editor out of the capture.
    if (auto* peer = getPeer())
    {
        auto* root = pc::rootWindowOf (peer->getNativeHandle());
        if (root != ownWindow)
        {
            engine.setOwnWindow (ownWindow, false);
            engine.setOwnWindow (root, true);
            ownWindow = root;
        }
    }

    for (auto& line : engine.logSince (nextLogLine))
        logView.append (line.text, colourFor (line.tag));

    const auto engineStatus = engine.status();
    if (engineStatus != shownStatus || status.getText().isEmpty())
    {
        shownStatus = engineStatus;
        const auto& style = statusStyles[(int) engineStatus];
        status.setText (utf8 (style.text), juce::dontSendNotification);
        status.setColour (juce::Label::textColourId, style.colour);
    }

    if (auto titles = engine.windows(); titles != windowTitles)
    {
        windowTitles = titles;
        capturePage.repaint (windowsPanel);
    }

    if (const auto scenes = engine.scenes(); scenes.generation != scenesGeneration)
    {
        scenesGeneration = scenes.generation;
        auto scene = sceneBox.getText().trim();
        sceneBox.clear (juce::dontSendNotification);
        sceneBox.addItemList (scenes.names, 1);
        if (scene.isEmpty() || (! scenes.names.contains (scene) && ! engine.isRunning()))
        {
            if (scene.isNotEmpty() && ! scenes.quiet)
                engine.log ("Scene '" + scene + "' no longer exists in OBS.", pc::Tag::Yellow);
            scene = scenes.current;
        }
        sceneBox.setText (scene, juce::dontSendNotification);
        if (! scenes.quiet)
            engine.log (juce::String (scenes.names.size()) + " scene(s) in OBS: " + scenes.names.joinIntoString (", "),
                        pc::Tag::Dim);
    }

    if (const auto test = engine.testResult(); test.generation != testGeneration)
    {
        testGeneration = test.generation;
        testButton.setEnabled (true);
        showConnectionMessage (test.message, test.ok ? green : red);
        if (test.ok)
            save();
    }

    setBusy (engine.isRunning());
}

//==============================================================================
void Editor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (panel);
    g.fillRect (0, 0, getWidth(), 30);

    // Logo: a large window with a smaller plugin window over it (same shapes as the app icon).
    struct Rect { int x0, y0, x1, y1; juce::Colour colour; };
    const Rect shapes[] = { { 3, 5, 25, 25, cyan }, { 5, 9, 23, 23, background }, { 13, 13, 29, 28, magenta },
                            { 15, 17, 27, 26, background }, { 17, 19, 25, 21, green } };
    const float scale = 0.5f, left = 10.0f, top = 7.0f;
    for (auto& r : shapes)
    {
        g.setColour (r.colour);
        g.fillRect (juce::Rectangle<float> (left + (float) r.x0 * scale, top + (float) r.y0 * scale,
                                            (float) (r.x1 - r.x0) * scale, (float) (r.y1 - r.y0) * scale));
    }
}

void Editor::resized()
{
    constexpr int barHeight = 30;
    int x = 36;
    for (auto* tab : { &captureTab, &connectionTab })
    {
        const int width = textWidth (tab->text, true) + 12;
        tab->setBounds (x, 0, width, barHeight);
        x += width;
    }
    status.setBounds (x + 14, 0, 220, barHeight);

    const auto pageBounds = getLocalBounds().withTrimmedTop (barHeight);
    capturePage.setBounds (pageBounds);
    connectionPage.setBounds (pageBounds);
    layoutCapturePage (pageBounds.getWidth(), pageBounds.getHeight());
    layoutConnectionPage (pageBounds.getWidth(), pageBounds.getHeight());
}

void Editor::layoutCapturePage (int width, int height)
{
    constexpr int margin = 12, rowHeight = 26;
    int x = margin, y = margin;
    auto place = [&] (juce::Component& c, int w, int gapAfter)
    {
        c.setBounds (x, y, w, rowHeight);
        x += w + gapAfter;
    };
    place (prompt, textWidth ("PS>", true) + 2, 0);
    place (sceneLabel, textWidth (sceneLabel.getText()), 8);
    place (sceneBox, 270, 8);
    for (auto* button : { &refreshButton, &startButton, &stopButton })
        place (*button, textWidth (button->getButtonText(), true) + 24, 6);

    y += rowHeight + 4;
    autostartToggle.setBounds (margin, y, textWidth (autostartToggle.getButtonText()) + 24, 22);
    target.setBounds (autostartToggle.getRight() + 14, y, width - autostartToggle.getRight() - 26, 22);

    const int top = y + 28, bottom = height - margin;
    windowsPanel = { width - margin - 280, top, 280, bottom - top };
    windowsHeader.setBounds (windowsPanel.getX() + 10, top + 8, 260, 18);
    logView.setBounds (margin, top, windowsPanel.getX() - 10 - margin, bottom - top);
}

void Editor::layoutConnectionPage (int width, int height)
{
    constexpr int margin = 12, rowHeight = 26;
    const int labelWidth = textWidth ("M") * 14, charWidth = textWidth ("M");
    int y = margin;

    auto header = [&] (juce::Label& label, int spaceBefore)
    {
        y += spaceBefore;
        label.setBounds (margin, y, 300, 20);
        y += 26;
    };
    auto row = [&] (juce::Label& label, juce::TextEditor& editor, int chars, juce::Label& hint, juce::Component* extra = nullptr)
    {
        label.setBounds (margin, y, labelWidth, rowHeight);
        const int editorX = margin + labelWidth;
        const int editorWidth = std::min (charWidth * chars + 12, width - editorX - margin);
        editor.setBounds (editorX, y, editorWidth, rowHeight);
        int hintX = editorX + editorWidth + 10;
        if (extra != nullptr)
        {
            extra->setBounds (hintX, y, 70, rowHeight);
            hintX += 74;
        }
        hint.setBounds (hintX, y, std::max (0, width - hintX - margin), rowHeight);
        y += 32;
    };

    header (websocketHeader, 0);
    row (hostLabel, hostEditor, 22, hostHint);
    row (portLabel, portEditor, 8, portHint);
    row (passwordLabel, passwordEditor, 30, passwordHint, &showPassword);
    savePassword.setBounds (margin + labelWidth, y - 2, textWidth (savePassword.getButtonText()) + 24, 22);
    savePasswordHint.setBounds (savePassword.getRight() + 4, y - 2, std::max (0, width - savePassword.getRight() - 16), 22);
    y += 30;

    header (privacyHeader, 6);
    row (ignoreLabel, ignoreEditor, 60, ignoreHint);

    y += 8;
    testButton.setBounds (margin + labelWidth, y, textWidth (testButton.getButtonText(), true) + 24, rowHeight);
    saveButton.setBounds (testButton.getRight() + 6, y, textWidth (saveButton.getButtonText(), true) + 24, rowHeight);
    connectionStatus.setBounds (saveButton.getRight() + 10, y, std::max (0, width - saveButton.getRight() - 22), rowHeight);

    y += rowHeight + 12;
    dividerY = y;
    y += 13;
    const int legalHeight = 52;
    help.setBounds (margin, y, width - 2 * margin, std::max (0, height - y - legalHeight - margin));
    legal.setBounds (margin, height - margin - legalHeight, width - 2 * margin, legalHeight);
}
