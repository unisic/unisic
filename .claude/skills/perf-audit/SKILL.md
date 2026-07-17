---
name: perf-audit
description: Full performance/concurrency audit of Unisic (C++/Qt6/QML, Wayland). Use when the user asks for a performance audit, "make app fast", idle CPU/memory investigation, memory leaks, thread bugs, deadlocks, or invokes /perf-audit. Measurement-first — never guess; produces a structured report and minimal fixes.
---

You are a senior Linux desktop performance engineer and Qt/C++ concurrency expert.

Audit this entire C++/Qt6/QML application (Unisic — a screenshot and screen recording tool for Linux Wayland, targeting KDE Plasma/KWin, niri, wlroots compositors, and GNOME/xdg-desktop-portal).

Main goal:

MAKE APP FAST.
MAKE APP QUIET WHEN IDLE.
STOP MEMORY GROWTH.
STOP CPU WASTE.
STOP THREAD BUGS.
DO NOT GUESS.
MEASURE EVERYTHING.

Check the application for:

* memory leaks
* retain/ownership cycles (QObject parent-child, shared_ptr cycles)
* unbounded memory growth
* excessive allocations
* abandoned objects (QObjects never deleted, dangling raw pointers)
* duplicated caches (e.g. thumbnail cache, history cache)
* oversized caches
* CPU leaks and runaway background work
* unnecessary wakeups
* busy loops
* excessive QTimer usage
* excessive QML re-renders / scene graph updates
* repeated layout passes (QML relayout, anchors, Repeater churn)
* repeated state updates (Q_PROPERTY NOTIFY storms)
* race conditions
* data races
* deadlocks
* livelocks
* priority inversions
* unsafe shared mutable state across threads
* incorrect main/GUI-thread usage (Qt objects touched off-thread)
* incorrect QThread / QtConcurrent / moveToThread usage
* incorrect locking (QMutex, QReadWriteLock, std::mutex)
* unnecessary work on the main/GUI thread
* excessive background tasks (capture pipeline, upload workers, history indexing)
* tasks that are never cancelled (uploads, ffmpeg processes, D-Bus calls)
* signal/slot connections that are never disconnected
* resources that are never released
* files, sockets, D-Bus connections, PipeWire streams, or ffmpeg subprocesses that stay open
* performance regressions caused by QML/Qt Quick or window lifecycle mistakes

The application is written using C++17/20, Qt6 (Core, Quick, QML, DBus, Concurrent), and integrates with:

* Wayland via xdg-desktop-portal (Screenshot, ScreenCast portals)
* KWin's native ScreenShot2 D-Bus interface on Plasma
* KGlobalAccel for global hotkeys on Plasma
* wlr-screencopy via `grim` subprocess on niri/wlroots compositors
* PipeWire for screen recording streams
* ffmpeg subprocess for GIF/video encoding (two-pass GIF palette, MP4/WebM)
* Uploaders: custom HTTP, .sxcu import, FTP/SFTP, built-in hosts

TARGETS

When the application is open but completely idle (main window/tray icon present, no capture/recording in progress):

* CPU usage should stay as close to 0% as practically possible
* there should be no constant CPU activity
* there should be no periodic CPU spikes without a valid reason
* there should be no unnecessary thread wakeups
* memory usage should remain stable
* memory must not continuously increase
* background tasks (uploaders, D-Bus watchers, PipeWire session) should sleep, suspend or stop
* timers should not run unless truly required
* the app should not continuously redraw, relayout, poll, or refresh
* no unnecessary network, disk, D-Bus, or ffmpeg work should happen

Do not claim that performance is good only because the code looks correct.

Prove every important conclusion with:

* code references
* profiler results (perf, Heaptrack, Qt Creator's built-in profiler/QML Profiler)
* sanitizer results (ASan, TSan, UBSan)
* allocation behavior
* object lifetime observations (QObject destroyed() logging)
* thread and task behavior
* before-and-after measurements

AUDIT PROCESS

1. Understand the architecture

Map:

* application lifecycle (main.cpp, QGuiApplication/QApplication setup, engine load)
* QQmlApplicationEngine and root QML component ownership
* QWindow / QQuickWindow ownership (main window, overlay/selection window, editor window, history window)
* QQuickItem / QML component ownership (editor canvas, toolbar, history grid)
* services and managers (capture service, recording service, upload service, hotkey manager, theme manager, settings manager)
* singleton usage (QML singletons, static/global C++ instances)
* signal/slot flow (direct vs queued connections, cross-thread signals)
* QtConcurrent / std::async / QThread usage
* D-Bus interfaces and watched signals (portal responses, KWin ScreenShot2, KGlobalAccel)
* PipeWire stream lifecycle
* subprocess lifecycle (`grim`, `ffmpeg`, AppImageUpdate/zsync checks)
* timers (QTimer instances: recording elapsed timer, debounce/throttle for uploads, retry/reconnect logic)
* event/hotkey monitors (KGlobalAccel bindings, compositor-bound shortcuts, `Ctrl+Esc` fixed stop handler)
* observers (QFileSystemWatcher for history folder, settings file watcher)
* caches (thumbnail cache, decoded image cache, theme assets)
* persistence (settings storage, history database/index, uploader credentials)
* network operations (upload requests, update checks)
* file watchers
* background workers

Identify which objects own which other objects.

Call out unclear or dangerous ownership relationships (raw pointers vs QPointer vs parent-child vs shared_ptr).

2. Find memory leaks

Inspect for:

* QObject parent/child ownership gaps (objects created with no parent, never deleted)
* lambda captures capturing `this`/`self` strongly across signal/slot or QtConcurrent boundaries
* delegates/callbacks that should use QPointer or weak capture
* parent-child ownership cycles between windows and their controllers/view-models
* QWindow / QQuickWindow cycles (overlay window referencing main window and vice versa)
* QML component lifecycle problems (Loader not unloading, Window not destroyed on close)
* dangling signal/slot connections after object destruction
* D-Bus signal watchers never disconnected
* QTimer objects retaining their receiver after the receiver should be gone
* PipeWire stream objects not released after recording stops
* ffmpeg/grim QProcess objects not deleted after finishing
* detached QtConcurrent::run tasks retaining owners
* completion handlers (upload finished, portal response) never released
* global and static object retention
* caches without eviction (thumbnail cache, history cache)
* image and pixmap/QImage retention (full-resolution screenshots kept after downscaled preview exists)
* large QByteArray/QString copies
* duplicated capture/editor models
* resources retained after closing the editor window or history window

Verify whether windows, QML items, models, and services are deallocated after they are no longer needed.

Add temporary `~Foo()` / `destroyed()` logging or lifetime diagnostics when useful.

Use tools:

* Heaptrack (heap allocation tracking + leak detection)
* Valgrind Memcheck (for the C++ core, where feasible given runtime cost)
* AddressSanitizer with LeakSanitizer (ASan+LSan build)
* GammaRay (Qt object inspector — view live QObject tree, connections, properties)

Test repeated workflows:

* open and close the editor window many times
* trigger and cancel region/window/full-screen capture many times
* start and stop recording many times
* load and unload large screenshots/recordings many times
* connect and disconnect uploaders many times
* switch themes many times

Memory may temporarily increase, but it must return to a stable plateau.

3. Find CPU waste

Use `perf top` / `perf record` + Qt Creator's QML Profiler.

Inspect CPU usage in these states:

* immediately after launch
* application idle with the main window/tray visible
* application idle with only the tray icon present (main window closed)
* application idle during an active recording (should still be near-zero except encoder work)
* during normal interaction (selecting a region, annotating)
* after completing a capture/recording
* after cancelling a capture/recording
* after closing the editor or history window

Look for:

* busy loops
* polling (e.g. polling PipeWire or D-Bus instead of using signals/callbacks)
* short repeating QTimers
* recursive `QTimer::singleShot` scheduling
* QtConcurrent loops without termination condition
* excessive GUI-thread hops via `QMetaObject::invokeMethod` with `Qt::QueuedConnection`
* unnecessary Q_PROPERTY NOTIFY emissions causing QML re-binding storms
* excessive D-Bus signal traffic
* repeated QML ListView/GridView model resets (history grid, theme picker)
* repeated layout calls (QML anchors recalculation, `Repeater` rebuilding)
* repeated `Q_PROPERTY` write→NOTIFY→QML binding evaluation chains
* excessive `update()`/`polish()` calls on QQuickItem
* continuous animations left running after they're visually irrelevant (e.g. REC badge timer)
* incorrect QTimer usage (interval too short, `Qt::PreciseTimer` where `Qt::CoarseTimer` would do)
* file system polling instead of QFileSystemWatcher
* network polling instead of long-lived connections/webhooks
* excessive logging (qDebug/qWarning in hot paths)
* expensive computed properties re-evaluated every frame
* repeated sorting/filtering of history, repeated thumbnail decoding
* duplicate work performed by multiple signal handlers connected to the same signal
* operations (encoding, upload) that continue after the relevant window/UI disappears

Find the exact stacks responsible for CPU use.

Do not optimize cold code.

Optimize verified hot paths first.

4. Check idle performance

Create a repeatable idle benchmark.

Idle means:

* no user input
* no animation expected
* no active capture/recording/upload
* no intentional history indexing
* no intentional background processing
* stable window contents (or no window open, tray-only)

Measure for at least several minutes.

Report:

* average CPU usage
* CPU spikes
* spike frequency
* thread wakeups (`perf sched`, `powertop`)
* active threads
* active QtConcurrent/QThread tasks
* timer activity (list every live QTimer and its interval)
* GUI-thread activity
* memory at the start
* memory at the end
* memory growth rate
* number of live objects for important classes (via GammaRay object inspector)

Find every source of recurring work.

For each source, explain:

* why it runs
* how often it runs
* whether it is necessary
* whether it can be event-driven (D-Bus signal, QFileSystemWatcher, PipeWire callback instead of polling)
* whether it can be cancelled
* whether it can use a longer interval
* whether it should stop when the app loses focus/is backgrounded
* whether it should stop when its window closes

Exact 0.00% CPU may not always be realistic because of the compositor, D-Bus daemon, and OS scheduler activity.

The real requirement is:

* no application-generated continuous CPU work
* no unnecessary periodic wakeups
* no unexplained CPU spikes
* sustained CPU usage as close to zero as the platform allows

5. Check concurrency

Run and analyze:

* ThreadSanitizer (TSan) build
* AddressSanitizer (ASan) build
* UndefinedBehaviorSanitizer (UBSan) build
* Qt's own thread-affinity assertions (`Q_ASSERT(thread() == qApp->thread())` patterns, or enable `QT_FATAL_WARNINGS` for cross-thread QObject warnings)
* strict `-Wall -Wextra -Wthread-safety` (if using Clang thread-safety annotations) where supported

Inspect:

* shared mutable state (settings singleton, capture buffer, upload queue)
* which QObjects are created on and belong to which thread
* GUI-thread boundary violations (touching QQuickItem/QWindow from a worker thread)
* thread-safety of D-Bus call callbacks (do they marshal back to the GUI thread?)
* PipeWire callback thread vs GUI thread interaction
* QtConcurrent::run / QThreadPool task safety
* detached threads (`QThread` without `wait()`/`quit()` on shutdown)
* mutexes and locks (QMutex, QRecursiveMutex, std::mutex, std::lock_guard)
* semaphores (QSemaphore) and condition variables
* `QMetaObject::invokeMethod` with `Qt::BlockingQueuedConnection` (blocking cross-thread calls)
* nested queue/lock synchronization
* GUI-thread blocking (any `wait()`, blocking D-Bus call, blocking network call on GUI thread)
* callback-to-signal bridges (subprocess finished → signal → slot)
* re-entrancy in signal handlers
* cancellation handling for QtConcurrent futures and subprocesses

Find potential data races even if ThreadSanitizer does not reproduce them.

For every shared mutable object, document:

* who reads it
* who writes it
* on which thread
* what synchronization protects it (or note if it relies on Qt's queued-connection thread affinity instead of explicit locks)

6. Find deadlocks

Search for dangerous patterns:

* `QMetaObject::invokeMethod(..., Qt::BlockingQueuedConnection)` called from the GUI thread targeting the GUI thread
* blocking D-Bus calls made synchronously from the GUI thread
* nested locks (QMutex held while acquiring another QMutex)
* inconsistent lock ordering
* `QSemaphore::acquire()` or `QWaitCondition::wait()` on the GUI thread
* waiting for a QtConcurrent future that itself needs the GUI thread to complete
* blocking inside a Q_INVOKABLE/slot connected via queued connection
* `QThread::wait()` called from the thread being waited on
* locks held while emitting signals
* locks held while calling D-Bus or subprocess APIs
* locks held while awaiting PipeWire callbacks
* synchronous callbacks that re-enter locked code
* circular dependencies between services (capture service waiting on upload service waiting on capture service, etc.)

Build a lock and thread/queue dependency map.

For every possible deadlock, show the exact execution sequence that can cause it.

7. Check Qt Quick / QML-specific performance problems

Inspect:

* `update()` / `polish()` call frequency on custom QQuickItems (selection overlay canvas, editor canvas)
* custom paint/`QSGNode` scene graph rebuild frequency
* layout and anchor recalculation frequency (editor toolbar, history grid)
* `implicitWidth`/`implicitHeight` recalculation storms
* `ListView`/`GridView`/`Repeater` model reset frequency (history grid, theme picker)
* delegate creation/destruction churn (cacheBuffer, reuseItems settings)
* window resize/show/hide handling (overlay window shown per-capture, editor window shown per-edit)
* global shortcut/event monitor registration (KGlobalAccel bindings, portal-based shortcuts, compositor-bound shortcuts)
* D-Bus signal subscriptions tied to window lifecycle
* undo/redo stack growth in the editor
* application activation/focus-change behavior (does anything keep running when unfocused?)
* tray-only / background behavior when all windows are closed

Make sure closed windows (overlay, editor, history) and their QML items are actually destroyed, not just hidden.

Make sure hidden windows do not continue performing expensive work (e.g. editor canvas still repainting behind a hidden window).

8. Check timers and recurring work

List every:

* QTimer / `QTimer::singleShot`
* recording elapsed-time timer (REC badge)
* debounce/throttle pipeline (e.g. settings autosave, upload retry backoff)
* retry/reconnect mechanism (uploader network retry, portal reconnect)
* QFileSystemWatcher usage
* PipeWire stream polling vs callback-driven reads
* ffmpeg/grim subprocess health checks
* AppImage update / zsync check timer

For each one report:

* interval
* timer type (`Qt::PreciseTimer`/`CoarseTimer`/`VeryCoarseTimer`)
* thread/owner
* cancellation mechanism
* conditions under which it stops
* whether it runs while the app is backgrounded/tray-only
* whether it runs after its window (overlay/editor/history) is closed
* whether it can be replaced by an event-driven mechanism (signal, D-Bus notification, file watcher)

9. Check memory efficiency

Look for:

* unnecessary QImage/QPixmap copies (implicit sharing defeated by detach())
* large temporary collections (full-resolution frame buffers during recording)
* repeated JSON decoding (settings, history index) on every access instead of caching
* repeated QDateTime/number formatter construction in loops
* repeated image decoding (thumbnails re-decoded from disk instead of cached)
* images/thumbnails stored at unnecessarily large resolution
* duplicate model objects (capture data held by both the pipeline and the editor)
* unbounded history (no cap on number of history entries kept in memory)
* unbounded logs
* unbounded arrays or QVariantMaps
* caches without count or byte-size limits (thumbnail cache, theme asset cache)
* data loaded eagerly instead of lazily (loading entire history on startup vs paginating)
* entire video files loaded when streaming to ffmpeg would work
* unnecessary bridging/copies between C++ and QML (QVariant boxing overhead in hot paths)
* objects created in tight loops without reuse (e.g. per-frame allocations during recording)

Recommend limits and eviction policies based on actual application behavior.

Do not remove useful caching if doing so increases CPU or I/O.

Balance RAM, CPU, disk and responsiveness.

10. Check responsiveness

Measure:

* launch time (cold start to first paint)
* time to first usable window/tray icon
* overlay window opening time (hotkey press → selection overlay visible)
* editor window opening time (capture taken → editor visible with image loaded)
* history grid scrolling smoothness
* GUI-thread stalls
* hangs (use `QT_ENABLE_LOGGING_RULES` / a hang detector, or `perf record` during interaction)
* synchronous I/O on the GUI thread (reading history files, writing settings)
* synchronous D-Bus calls on the GUI thread
* synchronous network calls on the GUI thread (upload triggered from GUI thread without QtConcurrent/QNetworkAccessManager's async API)
* expensive work during QML item paint/layout

No heavy work should block the GUI thread.

Use batching, lazy loading, or background processing (QtConcurrent, worker QThread) where appropriate.

Do not move Qt Quick scene-graph or window operations off the GUI thread.

11. Make fixes

For every confirmed problem:

* explain the root cause
* provide the exact file and symbol (class::method, or QML file + id)
* show the problematic code
* implement the smallest safe fix
* explain why the fix is correct
* explain possible side effects
* add a regression test or reproducible verification method
* measure before and after

Do not perform unrelated architecture rewrites.

Do not introduce abstractions without measurable benefit.

Do not trade correctness for lower resource usage.

Do not hide problems by increasing timer intervals without understanding the root cause.

Do not remove synchronization without proving thread safety.

12. Add performance diagnostics

Where useful, add debug-only diagnostics for:

* object deallocation (`destroyed()` logging for windows/services)
* task creation and cancellation (QtConcurrent futures, subprocess starts/finishes)
* active worker count
* cache size (thumbnail cache, history cache)
* timer creation and invalidation
* window and QML item lifetime
* repeated expensive operations
* GUI-thread blocking
* long-running D-Bus/PipeWire/subprocess operations

Use `QLoggingCategory` (categorized, filterable logging) and, where relevant, Linux perf USDT/tracepoints or Qt's own trace points.

Do not leave noisy logging enabled in production (guard behind a debug logging category, disabled by default).

13. Add tests

Create tests for:

* objects being released after use (window close, editor close)
* repeated window opening and closing (overlay, editor, history)
* task cancellation (recording cancel, upload cancel)
* timer invalidation
* signal/slot disconnection on object destruction
* cache limits (thumbnail cache eviction)
* race-prone state transitions (start/stop recording rapidly)
* concurrent access (multiple captures queued)
* repeated loading and unloading of screenshots/recordings
* memory stability over repeated operations
* performance of critical operations (capture→editor pipeline latency, GIF/video encode start latency)

Use deterministic tests where possible (Qt Test framework, `QSignalSpy` for async assertions).

OUTPUT FORMAT

Return the audit in this exact structure:

# Executive summary

Include:

* overall severity
* biggest CPU problem
* biggest memory problem
* biggest concurrency risk
* biggest deadlock risk
* expected improvement after fixes

# Baseline measurements

Provide a table:

| Scenario | CPU average | CPU peaks | Memory start | Memory end | Wakeups | Notes |
| -------- | ----------: | --------: | -----------: | ---------: | ------: | ----- |

# Critical findings

For every finding include:

* severity: critical, high, medium or low
* category
* file
* symbol
* evidence
* root cause
* user-visible impact
* proposed fix
* verification method

# Memory findings

# CPU findings

# Idle-performance findings

# Race-condition findings

# Deadlock findings

# GUI-thread and responsiveness findings

# Qt/QML lifecycle findings

# Timer and background-work inventory

Provide a table:

| Source | Interval | Owner | Thread | Stops correctly | Required | Recommendation |
| ------ | -------: | ----- | ------ | --------------- | -------- | -------------- |

# Changes implemented

For each change include a focused diff.

# Before-and-after results

Use the same benchmark scenarios as the baseline.

# Remaining risks

Clearly state anything that could not be proven.

# Final verdict

Answer these questions directly:

* Does memory stabilize?
* Are important objects deallocated?
* Is idle CPU close to zero?
* Are there unnecessary wakeups?
* Are there known data races?
* Are there potential deadlocks?
* Is the GUI thread responsive?
* What is the next highest-value optimization?

IMPORTANT RULES

DO NOT GUESS.

DO NOT SAY "PROBABLY FINE".

DO NOT CALL SOMETHING A LEAK WITHOUT EVIDENCE.

DO NOT CALL SOMETHING THREAD-SAFE WITHOUT EXPLAINING THE SYNCHRONIZATION.

DO NOT OPTIMIZE CODE THAT DOES NOT APPEAR IN MEASUREMENTS.

DO NOT MAKE MASSIVE REWRITES BEFORE ESTABLISHING A BASELINE.

FIRST MEASURE.

THEN FIND ROOT CAUSE.

THEN FIX.

THEN MEASURE AGAIN.

If you cannot run profilers or sanitizers in the current environment:

* still perform the static audit
* prepare exact build configurations (CMake flags for ASan/TSan/UBSan builds, Qt Creator profiler setup)
* provide exact manual test scenarios
* provide exact profiling tools/commands to run (`heaptrack ./unisic`, `perf record -g ./unisic`, `qmlprofiler`, GammaRay attach)
* explain what evidence must be collected
* clearly separate confirmed problems from suspected problems
* never fabricate benchmark results
