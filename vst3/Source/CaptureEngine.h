// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 DAW Plugin Capture contributors
#pragma once

#include <juce_core/juce_core.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

namespace pc
{
enum class Tag { Normal, Dim, Cyan, Green, Yellow, Red, Magenta };
enum class Status { Stopped, Connecting, Connected, Waiting };

struct LogLine
{
    juce::String text;
    Tag tag;
};

struct Config
{
    juce::String host = "localhost";
    int port = 4455;
    juce::String password, scene, ignore;
    double scanInterval = 0.25;     ///< seconds between scans for opened/closed windows
};

/**
    Mirrors the host DAW's plugin windows into OBS.

    Every visible, titled window of this process that is owned by another window (plugin editors and
    dialogs; the DAW's main window has no owner) gets WS_EX_TOOLWINDOW cleared so OBS can capture it,
    and a Window Capture source in the chosen scene, positioned where the window is on screen. Sources
    follow their windows at OBS's frame rate and are removed when the windows close; window styles are
    restored when capturing stops.

    One engine per process, shared by all plugin instances (acquire()). A named mutex prevents the
    plugin and the desktop app from capturing at the same time.
*/
class Engine
{
public:
    static std::shared_ptr<Engine> acquire();
    ~Engine();

    void start (const Config&);
    void stop();
    bool isRunning() const noexcept          { return running.load(); }

    void refreshScenes (const Config&, bool quiet);
    void testConnection (const Config&);

    /** Windows hosting this plugin's editor are never captured. */
    void setOwnWindow (void* hwnd, bool isOwn);

    void log (const juce::String& text, Tag tag = Tag::Normal);

    // Thread-safe snapshots for the editor ---------------------------------------------------------
    /** Lines from absolute index `next` onwards; `next` is advanced past them. */
    std::vector<LogLine> logSince (int& next) const;
    int logCount() const;
    Status status() const noexcept           { return state.load(); }
    juce::StringArray windows() const;

    struct Scenes { int generation = 0; juce::StringArray names; juce::String current; bool quiet = true; };
    Scenes scenes() const;

    struct TestResult { int generation = 0; bool ok = false; juce::String message; };
    TestResult testResult() const;

    bool autostartHandled = false;           ///< message thread only

private:
    Engine() = default;

    void run (Config);
    void connect (const Config&);
    void scan (const Config&);
    void follow (const Config&);
    juce::var settingsOf (const juce::String& inputName);
    void updateLayout (const juce::var& sceneItems);
    void restack (const Config&, const juce::var& sceneItems, const std::vector<void*>& frontToBack);
    void followUntil (const Config&, std::chrono::steady_clock::time_point nextScan);
    void cleanup (bool obsReachable);
    bool waitUntil (std::chrono::steady_clock::time_point);
    void setWindows (const juce::StringArray&);
    void runJob (std::function<void()>);

    mutable std::mutex lock;
    std::deque<LogLine> lines;
    int firstLineIndex = 0;
    juce::StringArray windowTitles;
    Scenes sceneInfo;
    TestResult lastTest;
    std::set<void*> ownWindows;

    std::atomic<Status> state { Status::Stopped };
    std::atomic<bool> running { false }, stopRequested { false };
    std::mutex waitMutex;
    std::condition_variable waitCondition;
    std::thread worker;

    struct Job
    {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::mutex jobMutex;
    std::vector<Job> jobs;

    struct CaptureState;                     ///< worker-thread state (Win32 types stay in the .cpp)
    std::unique_ptr<CaptureState> capture;
};

/** The top-level window containing `hwnd`. */
void* rootWindowOf (void* hwnd);

/** File name of the host executable, e.g. "Ableton Live 12 Suite.exe". */
juce::String hostExeName();
} // namespace pc
