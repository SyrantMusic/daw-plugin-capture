// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (C) 2026 DAW Plugin Capture contributors
#include "CaptureEngine.h"
#include "ObsClient.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <timeapi.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace pc
{
namespace
{
const juce::String sourcePrefix = "DAWCapture";                    // every OBS source created starts with this
const wchar_t* const mutexName = L"Local\\DAWPluginCapture.Engine";  // shared with the desktop app
constexpr size_t maxLogLines = 1000;

struct Cancelled
{
    juce::String message;
};

// Plugin windows belong to the DAW's UI thread, which is also the plugin's message thread. Anything that
// sends that thread a message from the worker must never run while the message thread waits for the worker.

juce::String windowText (HWND hwnd)
{
    wchar_t buffer[512];
    const int length = InternalGetWindowText (hwnd, buffer, 512);     // reads the title without WM_GETTEXT
    return juce::String (buffer, (size_t) std::max (length, 0));
}

juce::String className (HWND hwnd)
{
    wchar_t buffer[256];
    const int length = GetClassNameW (hwnd, buffer, 256);
    return juce::String (buffer, (size_t) std::max (length, 0));
}

/** Visible window bounds, excluding the invisible resize border and shadow. */
RECT frameRect (HWND hwnd)
{
    RECT r {};
    if (FAILED (DwmGetWindowAttribute (hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof (r))))
        GetWindowRect (hwnd, &r);
    return r;
}

RECT monitorRect (HWND hwnd)
{
    MONITORINFO info {};
    info.cbSize = sizeof (info);
    GetMonitorInfoW (MonitorFromWindow (hwnd, MONITOR_DEFAULTTONEAREST), &info);
    return info.rcMonitor;
}

LONG_PTR getExStyle (HWND hwnd)
{
    return GetWindowLongPtrW (hwnd, GWL_EXSTYLE);
}

void setExStyle (HWND hwnd, LONG_PTR exStyle)
{
    SetWindowLongPtrW (hwnd, GWL_EXSTYLE, exStyle);
    SetWindowPos (hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

bool isInThisProcess (HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId (hwnd, &pid);
    return pid == GetCurrentProcessId();
}

/** OBS's window_capture `window` setting: "title:class:exe" with '#' and ':' escaped. */
juce::String obsWindowString (const juce::String& title, const juce::String& cls, const juce::String& exe)
{
    auto escape = [] (const juce::String& s) { return s.replace ("#", "#22").replace (":", "#3A"); };
    return escape (title) + ":" + escape (cls) + ":" + exe;
}

bool isLocalHost (juce::String host)
{
    host = host.trim().toLowerCase();
    return host == "localhost" || host == "127.0.0.1" || host == "::1" || host == "[::1]";
}

juce::StringArray parseIgnoreList (const juce::String& text)
{
    juce::StringArray words;
    for (auto word : juce::StringArray::fromTokens (text, ",", ""))
        if ((word = word.trim().toLowerCase()).isNotEmpty())
            words.add (word);
    return words;
}

struct WindowInfo
{
    juce::String title, cls;
};

struct EnumContext
{
    const juce::StringArray& ignore;
    const std::set<void*>& ownWindows;
    std::map<HWND, WindowInfo>& found;
};

BOOL CALLBACK collectPluginWindow (HWND hwnd, LPARAM param)
{
    auto& context = *reinterpret_cast<EnumContext*> (param);
    if (! IsWindowVisible (hwnd) || IsIconic (hwnd) || GetWindow (hwnd, GW_OWNER) == nullptr
        || ! isInThisProcess (hwnd) || context.ownWindows.count (hwnd) != 0)
        return TRUE;

    const auto title = windowText (hwnd);
    if (title.isEmpty())
        return TRUE;
    const auto lowerTitle = title.toLowerCase();
    for (auto& word : context.ignore)
        if (lowerTitle.contains (word))
            return TRUE;

    // Standard Windows dialogs (file open/save, message boxes) show folder paths, which usually include
    // the Windows user name. Plugin editors use their own window classes.
    const auto cls = className (hwnd);
    if (cls == "#32770")
        return TRUE;

    const auto r = frameRect (hwnd);
    if (r.right - r.left >= 50 && r.bottom - r.top >= 50)
        context.found[hwnd] = { title, cls };
    return TRUE;
}

/** Puts back WS_EX_TOOLWINDOW unless the style changed since (or the handle was reused). */
void restoreStyle (HWND hwnd, LONG_PTR originalExStyle)
{
    if (IsWindow (hwnd) && isInThisProcess (hwnd) && getExStyle (hwnd) == (originalExStyle & ~(LONG_PTR) WS_EX_TOOLWINDOW))
        setExStyle (hwnd, originalExStyle);
}

/** Joins a thread that may be sending messages to this (UI) thread, handling those messages meanwhile
    so neither side waits for the other forever. Posted messages are left in the queue. */
void joinFromUiThread (std::thread& thread)
{
    if (! thread.joinable())
        return;
    const HANDLE handle = (HANDLE) thread.native_handle();
    while (MsgWaitForMultipleObjects (1, &handle, FALSE, INFINITE, QS_SENDMESSAGE) == WAIT_OBJECT_0 + 1)
    {
        MSG msg;
        PeekMessageW (&msg, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
    thread.join();
}

std::pair<juce::StringArray, juce::String> listScenes (const Config& config)
{
    ObsClient client (config.host, config.port, config.password, 3000);
    const auto response = client.request ("GetSceneList");
    juce::StringArray names;
    if (auto* scenes = response["scenes"].getArray())
        for (int i = scenes->size(); --i >= 0;)        // OBS lists bottom-up; show them in dock order
            names.add ((*scenes)[i]["sceneName"].toString());
    return { names, response["currentProgramSceneName"].toString() };
}
} // namespace

void* rootWindowOf (void* hwnd)
{
    return hwnd != nullptr ? (void*) GetAncestor ((HWND) hwnd, GA_ROOT) : nullptr;
}

juce::String hostExeName()
{
    wchar_t path[1024];
    const DWORD length = GetModuleFileNameW (nullptr, path, 1024);
    return juce::File (juce::String (path, (size_t) length)).getFileName();
}

//==============================================================================
struct Engine::CaptureState
{
    struct Tracked
    {
        juce::String sourceName, title;
        int sceneItemId = 0, order = 0;
        LONG_PTR originalExStyle = 0;
        std::array<long long, 3> lastTransform { LLONG_MIN, 0, 0 };
    };

    std::unique_ptr<ObsClient> obs;
    std::map<HWND, Tracked> tracked;
    int counter = 0, canvasWidth = 1920;
    double frameInterval = 1.0 / 60.0;
    juce::StringArray ignore;
    juce::String exeName;
};

std::shared_ptr<Engine> Engine::acquire()
{
    static std::mutex acquireMutex;
    static std::weak_ptr<Engine> instance;
    std::lock_guard<std::mutex> guard (acquireMutex);
    auto engine = instance.lock();
    if (engine == nullptr)
    {
        engine = std::shared_ptr<Engine> (new Engine());
        instance = engine;
    }
    return engine;
}

Engine::~Engine()
{
    stop();
    joinFromUiThread (worker);              // removes OBS sources and restores window styles

    std::lock_guard<std::mutex> guard (jobMutex);
    for (auto& job : jobs)
        if (job.thread.joinable())
            job.thread.join();
}

void Engine::start (const Config& config)
{
    if (running)
        return;
    joinFromUiThread (worker);
    stopRequested = false;
    running = true;
    worker = std::thread ([this, config]
    {
        run (config);
        running = false;
    });
}

void Engine::stop()
{
    {
        std::lock_guard<std::mutex> guard (waitMutex);
        stopRequested = true;
    }
    waitCondition.notify_all();
}

bool Engine::waitUntil (std::chrono::steady_clock::time_point deadline)
{
    std::unique_lock<std::mutex> guard (waitMutex);
    return waitCondition.wait_until (guard, deadline, [this] { return stopRequested.load(); });
}

void Engine::setOwnWindow (void* hwnd, bool isOwn)
{
    if (hwnd == nullptr)
        return;
    std::lock_guard<std::mutex> guard (lock);
    if (isOwn)
        ownWindows.insert (hwnd);
    else
        ownWindows.erase (hwnd);
}

void Engine::log (const juce::String& text, Tag tag)
{
    std::lock_guard<std::mutex> guard (lock);
    lines.push_back ({ text, tag });
    while (lines.size() > maxLogLines)
    {
        lines.pop_front();
        ++firstLineIndex;
    }
}

std::vector<LogLine> Engine::logSince (int& next) const
{
    std::lock_guard<std::mutex> guard (lock);
    const int end = firstLineIndex + (int) lines.size();
    std::vector<LogLine> result;
    for (int i = std::max (next, firstLineIndex); i < end; ++i)
        result.push_back (lines[(size_t) (i - firstLineIndex)]);
    next = end;
    return result;
}

int Engine::logCount() const
{
    std::lock_guard<std::mutex> guard (lock);
    return firstLineIndex + (int) lines.size();
}

juce::StringArray Engine::windows() const
{
    std::lock_guard<std::mutex> guard (lock);
    return windowTitles;
}

void Engine::setWindows (const juce::StringArray& titles)
{
    std::lock_guard<std::mutex> guard (lock);
    windowTitles = titles;
}

Engine::Scenes Engine::scenes() const
{
    std::lock_guard<std::mutex> guard (lock);
    return sceneInfo;
}

Engine::TestResult Engine::testResult() const
{
    std::lock_guard<std::mutex> guard (lock);
    return lastTest;
}

//==============================================================================
void Engine::runJob (std::function<void()> fn)
{
    std::lock_guard<std::mutex> guard (jobMutex);
    for (auto it = jobs.begin(); it != jobs.end();)
    {
        if (it->done->load())
        {
            it->thread.join();
            it = jobs.erase (it);
        }
        else
        {
            ++it;
        }
    }

    auto done = std::make_shared<std::atomic<bool>> (false);
    jobs.push_back ({ std::thread ([fn = std::move (fn), done]
                                   {
                                       try { fn(); } catch (...) {}
                                       *done = true;
                                   }),
                      done });
}

void Engine::refreshScenes (const Config& config, bool quiet)
{
    runJob ([this, config, quiet]
    {
        try
        {
            auto [names, current] = listScenes (config);
            std::lock_guard<std::mutex> guard (lock);
            sceneInfo = { sceneInfo.generation + 1, names, current, quiet };
        }
        catch (const ObsError& e)
        {
            if (! quiet)
                log (e.message(), Tag::Red);
        }
    });
}

void Engine::testConnection (const Config& config)
{
    runJob ([this, config]
    {
        try
        {
            auto [names, current] = listScenes (config);
            std::lock_guard<std::mutex> guard (lock);
            lastTest = { lastTest.generation + 1, true,
                         "OK - " + juce::String (names.size()) + " scene(s), live scene '" + current + "'." };
            sceneInfo = { sceneInfo.generation + 1, names, current, true };
        }
        catch (const ObsError& e)
        {
            std::lock_guard<std::mutex> guard (lock);
            lastTest = { lastTest.generation + 1, false, e.message() };
        }
    });
}

//==============================================================================
void Engine::connect (const Config& config)
{
    auto& s = *capture;
    s.obs = std::make_unique<ObsClient> (config.host, config.port, config.password, 5000);

    const auto video = s.obs->request ("GetVideoSettings");
    s.canvasWidth = std::max (1, (int) video["baseWidth"]);
    // Follow window moves once per OBS frame: faster can't show on stream, slower shows as lag.
    const double numerator = video["fpsNumerator"], denominator = video["fpsDenominator"];
    const double fps = denominator > 0 ? numerator / denominator : 60.0;
    s.frameInterval = 1.0 / juce::jlimit (30.0, 240.0, fps);

    const auto sceneList = s.obs->request ("GetSceneList");
    juce::StringArray sceneNames;
    if (auto* list = sceneList["scenes"].getArray())
        for (auto& scene : *list)
            sceneNames.add (scene["sceneName"].toString());
    if (! sceneNames.contains (config.scene))
        throw Cancelled { "Scene '" + config.scene + "' doesn't exist in OBS. Pick one from the list." };

    int removed = 0;
    const auto inputList = s.obs->request ("GetInputList");
    if (auto* inputs = inputList["inputs"].getArray())
    {
        for (auto& input : *inputs)
        {
            const auto name = input["inputName"].toString();
            if (name.startsWith (sourcePrefix))
            {
                s.obs->request ("RemoveInput", J ({ { "inputName", name } }));
                ++removed;
            }
        }
    }
    if (removed > 0)
        log ("Removed " + juce::String (removed) + " leftover source(s) from an earlier run.", Tag::Dim);

    state = Status::Connected;
    log ("Connected to OBS at " + config.host + ":" + juce::String (config.port) + "  ->  scene '" + config.scene
             + "'  (canvas " + juce::String (s.canvasWidth) + "px wide)",
         Tag::Green);
}

void Engine::follow (const Config& config)
{
    auto& s = *capture;
    for (auto& [hwnd, window] : s.tracked)
    {
        if (! IsWindow (hwnd))
            continue;                               // removed by the next scan

        const auto frame = frameRect (hwnd);
        const auto monitor = monitorRect (hwnd);
        // The OBS canvas is assumed to show the whole monitor the window is on.
        const double scale = s.canvasWidth / (double) std::max (1L, monitor.right - monitor.left);
        const double x = (frame.left - monitor.left) * scale;
        const double y = (frame.top - monitor.top) * scale;

        const std::array<long long, 3> key { std::llround (x * 10), std::llround (y * 10), std::llround (scale * 1000) };
        if (key == window.lastTransform)
            continue;

        s.obs->request ("SetSceneItemTransform",
                        J ({ { "sceneName", config.scene },
                             { "sceneItemId", window.sceneItemId },
                             { "sceneItemTransform", J ({ { "positionX", x }, { "positionY", y },
                                                          { "scaleX", scale }, { "scaleY", scale },
                                                          { "rotation", 0.0 }, { "alignment", 5 },
                                                          { "boundsType", "OBS_BOUNDS_NONE" } }) } }));
        window.lastTransform = key;
    }
}

void Engine::followUntil (const Config& config, std::chrono::steady_clock::time_point nextScan)
{
    using Clock = std::chrono::steady_clock;
    const auto frame = std::chrono::duration_cast<Clock::duration> (std::chrono::duration<double> (capture->frameInterval));
    // Fixed deadlines, so small overshoots don't add up.
    for (auto tick = Clock::now() + frame; ! waitUntil (tick) && tick < nextScan; tick += frame)
        follow (config);
}

void Engine::scan (const Config& config)
{
    auto& s = *capture;
    std::set<void*> excluded;
    {
        std::lock_guard<std::mutex> guard (lock);     // not held while touching windows (see joinFromUiThread)
        excluded = ownWindows;
    }
    std::map<HWND, WindowInfo> current;
    EnumContext context { s.ignore, excluded, current };
    EnumWindows (collectPluginWindow, (LPARAM) &context);
    bool changed = false;

    for (auto& [hwnd, info] : current)
    {
        auto existing = s.tracked.find (hwnd);
        if (existing == s.tracked.end())
        {
            const auto originalExStyle = getExStyle (hwnd);
            if ((originalExStyle & WS_EX_TOOLWINDOW) != 0)
                setExStyle (hwnd, originalExStyle & ~(LONG_PTR) WS_EX_TOOLWINDOW);

            const auto sourceName = (sourcePrefix + " " + juce::String (++s.counter) + ": " + info.title).substring (0, 200);
            const auto created = s.obs->request ("CreateInput",
                J ({ { "sceneName", config.scene },
                     { "inputName", sourceName },
                     { "inputKind", "window_capture" },
                     { "inputSettings", J ({ { "window", obsWindowString (info.title, info.cls, s.exeName) },
                                             { "method", 2 },       // Windows 10 (1903 and up) capture
                                             { "priority", 1 },     // match by window title
                                             { "cursor", false } }) },
                     { "sceneItemEnabled", true } }));

            CaptureState::Tracked window;
            window.sourceName = sourceName;
            window.title = info.title;
            window.sceneItemId = (int) created["sceneItemId"];
            window.order = s.counter;
            window.originalExStyle = originalExStyle;
            s.tracked[hwnd] = window;
            log ("+ " + info.title, Tag::Cyan);
            changed = true;
        }
        else if (existing->second.title != info.title)    // e.g. the track was renamed: retarget the capture
        {
            s.obs->request ("SetInputSettings",
                            J ({ { "inputName", existing->second.sourceName },
                                 { "inputSettings", J ({ { "window", obsWindowString (info.title, info.cls, s.exeName) } }) },
                                 { "overlay", true } }));
            log ("~ " + existing->second.title + "  ->  " + info.title, Tag::Dim);
            existing->second.title = info.title;
            changed = true;
        }
    }

    follow (config);

    for (auto it = s.tracked.begin(); it != s.tracked.end();)
    {
        if (current.count (it->first) != 0)
        {
            ++it;
            continue;
        }

        const auto hwnd = it->first;
        const auto window = it->second;
        it = s.tracked.erase (it);
        try
        {
            s.obs->request ("RemoveInput", J ({ { "inputName", window.sourceName } }));
        }
        catch (const ObsError& e)
        {
            if (e.kind != ObsError::Request)        // a missing source is fine; a lost connection isn't
                throw;
        }
        restoreStyle (hwnd, window.originalExStyle);     // DAWs often hide closed plugin windows
        log ("- " + window.title, Tag::Magenta);
        changed = true;
    }

    if (changed)
    {
        std::vector<const CaptureState::Tracked*> ordered;
        for (auto& entry : s.tracked)
            ordered.push_back (&entry.second);
        std::sort (ordered.begin(), ordered.end(), [] (auto* a, auto* b) { return a->order < b->order; });

        juce::StringArray titles;
        for (auto* window : ordered)
            titles.add (window->title);
        setWindows (titles);
    }
}

void Engine::cleanup (bool obsReachable)
{
    auto& s = *capture;
    for (auto& [hwnd, window] : s.tracked)
    {
        if (obsReachable && s.obs != nullptr)
        {
            try { s.obs->request ("RemoveInput", J ({ { "inputName", window.sourceName } })); }
            catch (const ObsError&) {}
        }
        restoreStyle (hwnd, window.originalExStyle);
    }
    s.tracked.clear();
    setWindows ({});
    s.obs.reset();
}

void Engine::run (Config config)
{
    capture = std::make_unique<CaptureState>();
    capture->ignore = parseIgnoreList (config.ignore);
    capture->exeName = hostExeName();
    SetThreadDpiAwarenessContext (DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);   // physical pixels

    const HANDLE mutex = CreateMutexW (nullptr, FALSE, mutexName);
    if (mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle (mutex);
        log ("DAW Plugin Capture is already capturing (in the app or another DAW). Stop it there first.", Tag::Red);
        state = Status::Stopped;
        return;
    }
    if (! isLocalHost (config.host))
        log ("Note: OBS WebSocket traffic is unencrypted. Only connect to another PC over a network you trust.", Tag::Yellow);

    timeBeginPeriod (1);     // 1 ms timer resolution; the default rounds 16.7 ms waits up to ~31 ms
    try
    {
        while (! stopRequested)
        {
            try
            {
                if (capture->obs == nullptr)
                {
                    state = Status::Connecting;
                    connect (config);
                }
                const auto nextScan = std::chrono::steady_clock::now()
                                    + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
                                          std::chrono::duration<double> (config.scanInterval));
                scan (config);
                followUntil (config, nextScan);
            }
            catch (const ObsError& e)
            {
                if (e.isFatal())
                    throw Cancelled { e.message() };
                log (e.message(), Tag::Red);
                cleanup (false);
                state = Status::Waiting;
                log ("Retrying in 3 s... (press Stop to give up)", Tag::Dim);
                if (waitUntil (std::chrono::steady_clock::now() + std::chrono::seconds (3)))
                    break;
            }
        }
    }
    catch (const Cancelled& c)
    {
        log (c.message, Tag::Red);
    }
    catch (const std::exception& e)
    {
        log ("Unexpected error: " + juce::String::fromUTF8 (e.what()), Tag::Red);
    }

    cleanup (true);
    timeEndPeriod (1);
    if (mutex != nullptr)
        CloseHandle (mutex);
    state = Status::Stopped;
    log ("Stopped. Plugin sources removed and window styles restored.", Tag::Yellow);
}
} // namespace pc
