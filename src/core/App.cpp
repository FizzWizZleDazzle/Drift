#include <drift/App.hpp>
#include <drift/Log.hpp>
#include <drift/World.hpp>
#include <drift/Commands.hpp>
#include <drift/WorldResource.h>
#include <drift/RenderSnapshot.hpp>
#include <drift/AssetServer.hpp>
#include <drift/resources/Time.hpp>

#include <SDL3/SDL.h>

#include "gfx/Device.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <unordered_map>
#include <map>
#include <string>
#include <typeindex>
#include <vector>
#include <algorithm>
#include <utility>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace drift {

namespace {

// ---- Thread pool: runs a batch of tasks, main thread participates ----
class ThreadPool {
public:
    explicit ThreadPool(int numThreads)
        : stop_(false)
    {
        for (int i = 0; i < numThreads; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    ~ThreadPool() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            cv_.notify_all();
        }
        for (auto& t : workers_) {
            t.join();
        }
    }

    // Run all tasks, blocking until complete. Main thread takes one task too.
    void runBatch(std::vector<std::function<void()>>& tasks) {
        if (tasks.empty()) return;

        {
            std::lock_guard<std::mutex> lock(mutex_);
            remaining_.store(static_cast<int>(tasks.size()), std::memory_order_relaxed);
            batchTasks_ = &tasks;
            nextTask_.store(1, std::memory_order_relaxed);  // main takes index 0
            batchGen_.fetch_add(1, std::memory_order_relaxed);
            cv_.notify_all();
        }

        // Main thread executes task 0
        try {
            tasks[0]();
        } catch (const std::exception& e) {
            DRIFT_LOG_ERROR("ThreadPool: main-thread task threw: %s", e.what());
        } catch (...) {
            DRIFT_LOG_ERROR("ThreadPool: main-thread task threw unknown exception");
        }
        remaining_.fetch_sub(1, std::memory_order_acq_rel);

        // Wait for all tasks to complete (with deadlock detection)
        std::unique_lock<std::mutex> lock(mutex_);
        while (remaining_.load(std::memory_order_acquire) != 0) {
            auto status = doneCv_.wait_for(lock, std::chrono::seconds(5));
            if (status == std::cv_status::timeout && remaining_.load(std::memory_order_acquire) != 0) {
                DRIFT_LOG_ERROR("ThreadPool: possible deadlock: remaining=%d, batchGen=%llu, tasks=%d",
                                remaining_.load(std::memory_order_acquire),
                                (unsigned long long)batchGen_.load(std::memory_order_relaxed),
                                batchTasks_ ? (int)batchTasks_->size() : 0);
            }
        }

        batchTasks_ = nullptr;
    }

    int threadCount() const { return static_cast<int>(workers_.size()) + 1; }  // +1 for main

private:
    void workerLoop() {
        uint64_t lastGen = 0;
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this, &lastGen] {
                    return stop_ || (batchTasks_ != nullptr &&
                                     batchGen_.load(std::memory_order_relaxed) != lastGen);
                });
                if (stop_ && !batchTasks_) return;
                if (!batchTasks_) continue;

                int idx = nextTask_.fetch_add(1, std::memory_order_relaxed);
                if (idx >= static_cast<int>(batchTasks_->size())) {
                    lastGen = batchGen_.load(std::memory_order_relaxed);
                    continue;
                }
                task = (*batchTasks_)[idx];
                lastGen = batchGen_.load(std::memory_order_relaxed);
            }

            if (task) {
                try {
                    task();
                } catch (const std::exception& e) {
                    DRIFT_LOG_ERROR("ThreadPool: worker task threw: %s", e.what());
                } catch (...) {
                    DRIFT_LOG_ERROR("ThreadPool: worker task threw unknown exception");
                }
                int prev = remaining_.fetch_sub(1, std::memory_order_acq_rel);
                if (prev == 1) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    doneCv_.notify_one();
                }
            }
        }
    }

    std::vector<std::thread> workers_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable doneCv_;
    bool stop_;
    std::vector<std::function<void()>>* batchTasks_ = nullptr;
    std::atomic<int> nextTask_{0};
    std::atomic<int> remaining_{0};
    std::atomic<uint64_t> batchGen_{0};
};

// ---- Thread-safe entity allocator wrapping World's allocator ----
class ThreadSafeAllocator : public EntityAllocator {
public:
    explicit ThreadSafeAllocator(EntityAllocator& inner) : inner_(inner) {}

    EntityId allocate() override {
        std::lock_guard<std::mutex> lock(mutex_);
        return inner_.allocate();
    }

private:
    EntityAllocator& inner_;
    std::mutex mutex_;
};

} // anonymous namespace

// Thread-local pointer to the per-thread Commands buffer (nullptr on main thread)
static thread_local Commands* tl_commands = nullptr;

// ---- System entry stored in the scheduler ----
struct SystemEntry {
    std::string name;
    Phase phase;
    std::vector<AccessDescriptor> deps;
    std::function<void()> fn;             // typed system function
    std::function<void(App&)> lambdaFn;   // lambda shorthand
    RunCondition runCondition;            // optional run condition
    bool isLambda = false;
    SystemSetId systemSet = -1;           // -1 = no set

    // Ordering constraints (Phase 5)
    std::vector<std::string> mustRunAfter;
    std::vector<std::string> mustRunBefore;

    // Profiling
    uint64_t lastDurationUs = 0;

    // Resolved on first parallel run: accesses a main-thread-only resource.
    int8_t mainThread = -1;
};

// Key for onEnter/onExit maps: (type_index of enum, int-cast enum value)
using StateKey = std::pair<std::type_index, int>;

struct App::Impl {
    Config config;

    SDL_Window* window = nullptr;
    std::unique_ptr<gfx::Device> device;
    Time* timeRes = nullptr;

    bool running = false;
    bool initialised = false;
    bool started = false;
    bool frameOpen = false;   // beginFrame() ran, endFrame() not yet

    // Phase durations of the current frame, in microseconds
    uint64_t preUpdateUs = 0, updateUs = 0, postUpdateUs = 0;

    // ECS World + Commands (owned by App)
    World world;
    std::unique_ptr<ThreadSafeAllocator> tsAllocator;
    Commands commands{world, world.componentRegistry(), &world};

    // Per-thread command buffers for parallel dispatch
    std::vector<std::unique_ptr<Commands>> threadCommands;

    // Thread pool (created in run())
    std::unique_ptr<ThreadPool> threadPool;

    // Frame timing
    uint64_t lastCounter = 0;
    uint64_t perfFrequency = 0;
    float dt = 0.f;
    float fps = 0.f;
    float smoothedFps = 0.f;
    uint64_t frame = 0;
    uint64_t startCounter = 0;

    // Resources
    std::unordered_map<std::type_index, Resource*> resourcesByType;
    std::vector<Resource*> ownedResources;

    // Systems grouped by phase
    std::vector<SystemEntry> startupSystems;
    std::vector<SystemEntry> preUpdateSystems;
    std::vector<SystemEntry> updateSystems;
    std::vector<SystemEntry> postUpdateSystems;
    std::vector<SystemEntry> extractSystems;
    std::vector<SystemEntry> renderSystems;
    std::vector<SystemEntry> renderFlushSystems;

    // State management
    std::map<StateKey, std::vector<SystemEntry>> onEnterSystems;
    std::map<StateKey, std::vector<SystemEntry>> onExitSystems;
    std::vector<std::function<void()>> stateTransitionProcessors;
    std::vector<std::function<void()>> initialStateEnters;

    // Event handlers
    std::vector<EventHandler*> eventHandlers;

    // Event update callbacks (called at frame start to swap buffers)
    std::vector<std::function<void()>> eventUpdateFns;

    // System set ordering
    std::unordered_map<int, std::vector<SystemSetId>> setOrderByPhase;

    // Plugins (owned)
    std::vector<Plugin*> plugins;
    std::vector<PluginGroup*> pluginGroups;

    std::vector<SystemEntry>& systemsForPhase(Phase phase) {
        switch (phase) {
            case Phase::Startup:     return startupSystems;
            case Phase::PreUpdate:   return preUpdateSystems;
            case Phase::Update:      return updateSystems;
            case Phase::PostUpdate:  return postUpdateSystems;
            case Phase::Extract:     return extractSystems;
            case Phase::Render:      return renderSystems;
            case Phase::RenderFlush: return renderFlushSystems;
        }
        return updateSystems;
    }

    // Sequential execution (used for Startup, Extract, Render, RenderFlush, and onEnter/onExit)
    void runSystems(std::vector<SystemEntry>& systems, App& app) {
        for (auto& sys : systems) {
            if (sys.runCondition && !sys.runCondition(app)) {
                continue;
            }

            uint64_t t0 = SDL_GetPerformanceCounter();
            if (sys.isLambda) {
                sys.lambdaFn(app);
            } else if (sys.fn) {
                sys.fn();
            }
            uint64_t t1 = SDL_GetPerformanceCounter();
            sys.lastDurationUs = (t1 - t0) * 1000000 / perfFrequency;
        }
    }

    // ---- Parallel scheduler (Phase 4) ----

    // A system is exclusive if it needs full App access (lambda) or has no tracked deps
    static bool isExclusive(const SystemEntry& sys) {
        return sys.isLambda || (!sys.fn);
    }

    // Two systems conflict if they share any typeId where at least one is Write
    static bool conflicts(const SystemEntry& a, const SystemEntry& b) {
        for (auto& da : a.deps) {
            for (auto& db : b.deps) {
                if (da.typeId == db.typeId &&
                    (da.mode == AccessMode::Write || db.mode == AccessMode::Write)) {
                    return true;
                }
            }
        }
        return false;
    }

    // Check if system has an ordering dependency on another system name
    static bool hasOrderDep(const SystemEntry& sys, const std::string& depName) {
        for (auto& name : sys.mustRunAfter) {
            if (name == depName) return true;
        }
        return false;
    }

    // Runs a phase. With `progress`, starts from and records which systems
    // have run, so a later call can resume the phase. With `deferMain`,
    // systems that need the main thread (and systems ordered after them by
    // a conflict) are left for that later call. Returns true once every
    // system of the phase has run.
    bool runSystemsParallel(std::vector<SystemEntry>& systems, App& app,
                            std::vector<char>* progress = nullptr, bool deferMain = false) {
        if (systems.empty()) return true;

        // No thread pool: the same order, one system at a time.
        if ((!threadPool || threadPool->threadCount() <= 1) && !progress) {
            runSystems(systems, app);
            return true;
        }
        const bool pooled = threadPool && threadPool->threadCount() > 1;

        int n = static_cast<int>(systems.size());
        std::vector<char> local;
        std::vector<char>& completed = progress ? *progress : local;
        if (static_cast<int>(completed.size()) != n) completed.assign(n, 0);
        int completedCount = 0;
        for (char c : completed) completedCount += c ? 1 : 0;

        for (auto& sys : systems) {
            if (sys.mainThread >= 0) continue;
            sys.mainThread = 0;
            for (auto& d : sys.deps) {
                if (d.kind != AccessKind::Resource) continue;
                auto it = resourcesByType.find(d.typeId);
                if (it != resourcesByType.end() && it->second->mainThreadOnly()) {
                    sys.mainThread = 1;
                    break;
                }
            }
        }

        while (completedCount < n) {
            // Build a batch of non-conflicting systems
            std::vector<int> batch;
            bool batchIsExclusive = false;

            for (int i = 0; i < n; ++i) {
                if (completed[i]) continue;

                auto& sys = systems[i];

                // Check run condition - skip disabled systems
                if (sys.runCondition && !sys.runCondition(app)) {
                    completed[i] = true;
                    completedCount++;
                    continue;
                }

                // Left for the main thread.
                if (deferMain && sys.mainThread == 1) continue;

                bool exclusive = isExclusive(sys);

                // If exclusive, it must run alone
                if (exclusive) {
                    if (batch.empty()) {
                        batch.push_back(i);
                        batchIsExclusive = true;
                        break;
                    }
                    // Can't add exclusive to non-empty batch
                    continue;
                }

                // If batch already has an exclusive, skip
                if (batchIsExclusive) continue;

                // Only one main-thread system per batch (it becomes task 0)
                if (sys.mainThread == 1) {
                    bool haveMain = false;
                    for (int j : batch) haveMain = haveMain || systems[j].mainThread == 1;
                    if (haveMain) continue;
                }

                // Check conflicts with systems already in batch
                bool conflictsWithBatch = false;
                for (int j : batch) {
                    if (conflicts(sys, systems[j])) {
                        conflictsWithBatch = true;
                        break;
                    }
                }
                if (conflictsWithBatch) continue;

                // Check ordering: must not run before any uncompleted predecessor
                bool blockedByOrder = false;
                for (int p = 0; p < i; ++p) {
                    if (completed[p]) continue;
                    // An exclusive system may touch anything, so nothing
                    // registered after it runs before it.
                    if (isExclusive(systems[p])) {
                        blockedByOrder = true;
                        break;
                    }
                    // If there's a conflict with an uncompleted predecessor, skip
                    if (conflicts(sys, systems[p])) {
                        blockedByOrder = true;
                        break;
                    }
                    // Check explicit ordering constraints
                    if (!sys.mustRunAfter.empty()) {
                        if (hasOrderDep(sys, systems[p].name)) {
                            blockedByOrder = true;
                            break;
                        }
                    }
                }
                if (blockedByOrder) continue;

                // Check if any uncompleted system has mustRunBefore pointing at us
                for (int p = 0; p < n; ++p) {
                    if (completed[p] || p == i) continue;
                    for (auto& bname : systems[p].mustRunBefore) {
                        if (bname == sys.name) {
                            blockedByOrder = true;
                            break;
                        }
                    }
                    if (blockedByOrder) break;
                }
                if (blockedByOrder) continue;

                batch.push_back(i);
                // Without a pool, one system per batch, in order.
                if (!pooled) break;
            }

            if (batch.empty()) break;  // the rest is deferred (or nothing can run)

            // The main thread runs task 0.
            for (size_t j = 1; j < batch.size(); ++j) {
                if (systems[batch[j]].mainThread == 1) {
                    std::swap(batch[0], batch[j]);
                    break;
                }
            }

            if (batch.size() == 1) {
                // Single system: run on main thread directly
                auto& sys = systems[batch[0]];
                uint64_t t0 = SDL_GetPerformanceCounter();
                if (sys.isLambda) {
                    sys.lambdaFn(app);
                } else if (sys.fn) {
                    sys.fn();
                }
                sys.lastDurationUs = (SDL_GetPerformanceCounter() - t0) * 1000000 / perfFrequency;
            } else {
                // Multiple systems: dispatch in parallel
                std::vector<std::function<void()>> tasks;
                tasks.reserve(batch.size());

                for (int idx : batch) {
                    auto& sys = systems[idx];
                    int threadSlot = static_cast<int>(tasks.size());
                    tasks.emplace_back([this, &sys, threadSlot]() {
                        // Set thread-local commands pointer
                        if (threadSlot < static_cast<int>(threadCommands.size())) {
                            tl_commands = threadCommands[threadSlot].get();
                        }
                        uint64_t t0 = SDL_GetPerformanceCounter();
                        if (sys.fn) {
                            sys.fn();
                        }
                        sys.lastDurationUs = (SDL_GetPerformanceCounter() - t0) * 1000000 / perfFrequency;
                        tl_commands = nullptr;
                    });
                }

                threadPool->runBatch(tasks);

                // Flush per-thread command buffers in system insertion order
                for (int idx : batch) {
                    (void)idx;
                }
                for (auto& tc : threadCommands) {
                    tc->flush(world);
                }
            }

            for (int idx : batch) {
                completed[idx] = true;
                completedCount++;
            }
        }
        return completedCount >= n;
    }

    // ---- Simulation (Update + PostUpdate), resumable ----

    // 0: nothing to run, 1: Update next, 2: PostUpdate next.
    int simStage = 0;
    std::vector<char> simProgress;
    SimulationTiming simTiming;

    // Runs the simulation phases from where they stopped. With deferMain,
    // stops at the first phase that has to wait for the main thread, so a
    // later phase never runs before an earlier one finished.
    void advanceSimulation(App& app, bool deferMain) {
        while (simStage == 1 || simStage == 2) {
            auto& systems = simStage == 1 ? updateSystems : postUpdateSystems;
            uint64_t t0 = SDL_GetPerformanceCounter();
            bool done = runSystemsParallel(systems, app, &simProgress, deferMain);
            commands.flush(world);
            uint64_t us = (SDL_GetPerformanceCounter() - t0) * 1000000 / perfFrequency;
            (simStage == 1 ? updateUs : postUpdateUs) += us;
            if (!done) return;
            simProgress.clear();
            simStage = simStage == 1 ? 2 : 0;
        }
    }

    // The background thread simulateAsync() hands the simulation to.
    std::thread simThread;
    std::mutex simMutex;
    std::condition_variable simCv;
    bool simRequested = false;   // a simulation is queued or running
    bool simStop = false;
    App* simApp = nullptr;

    void simLoop() {
        std::unique_lock<std::mutex> lock(simMutex);
        while (true) {
            simCv.wait(lock, [this] { return simStop || simRequested; });
            if (simStop) return;
            lock.unlock();
            simTiming.start = SDL_GetPerformanceCounter();
            advanceSimulation(*simApp, true);
            simTiming.end = SDL_GetPerformanceCounter();
            lock.lock();
            simRequested = false;
            simCv.notify_all();
        }
    }

    void waitSimThread() {
        std::unique_lock<std::mutex> lock(simMutex);
        simCv.wait(lock, [this] { return !simRequested; });
    }

    void stopSimThread() {
        if (!simThread.joinable()) return;
        {
            std::lock_guard<std::mutex> lock(simMutex);
            simStop = true;
        }
        simCv.notify_all();
        simThread.join();
    }
};

App::App() : impl_(std::make_unique<Impl>()) {}

App::~App() {
    // Stop the simulation thread and the pool first (joins threads before
    // any resources are freed)
    impl_->waitSimThread();
    impl_->stopSimThread();
    impl_->threadPool.reset();
    impl_->threadCommands.clear();
    impl_->tsAllocator.reset();

    // Clean up resources in reverse order (later resources may depend on earlier ones)
    for (auto it = impl_->ownedResources.rbegin(); it != impl_->ownedResources.rend(); ++it) {
        delete *it;
    }
    // Clean up plugins
    for (auto* p : impl_->plugins) delete p;
    for (auto* g : impl_->pluginGroups) delete g;

    // Shutdown SDL
    impl_->device.reset();
    if (impl_->window) {
        SDL_DestroyWindow(impl_->window);
    }
    if (impl_->initialised) {
        SDL_Quit();
    }
}

App& App::setConfig(const Config& cfg) {
    impl_->config = cfg;
    return *this;
}

App& App::addPlugin(Plugin* plugin) {
    if (!plugin) return *this;
    impl_->plugins.push_back(plugin);
    // build() is deferred until run(), after SDL init
    return *this;
}

App& App::addPlugins(PluginGroup* group) {
    if (!group) return *this;
    impl_->pluginGroups.push_back(group);
    // build() is deferred until run(), after SDL init
    return *this;
}

void App::addEventHandler(EventHandler* handler) {
    if (handler) impl_->eventHandlers.push_back(handler);
}

void App::addSystem(const char* name, Phase phase, std::function<void(App&)> fn) {
    SystemEntry entry;
    entry.name = name ? name : "";
    entry.phase = phase;
    entry.lambdaFn = std::move(fn);
    entry.isLambda = true;
    impl_->systemsForPhase(phase).push_back(std::move(entry));
}

void App::addResourceImpl(std::type_index type, Resource* res) {
    impl_->resourcesByType[type] = res;
    impl_->ownedResources.push_back(res);
}

Resource* App::getResourceImpl(std::type_index type) const {
    auto it = impl_->resourcesByType.find(type);
    return it != impl_->resourcesByType.end() ? it->second : nullptr;
}

void App::registerSystemFn(const char* name, Phase phase,
                           const std::vector<AccessDescriptor>& deps,
                           std::function<void()> fn) {
    SystemEntry entry;
    entry.name = name ? name : "";
    entry.phase = phase;
    entry.deps = deps;
    entry.fn = std::move(fn);
    impl_->systemsForPhase(phase).push_back(std::move(entry));
}

void App::registerSystemFn(const char* name, Phase phase,
                           const std::vector<AccessDescriptor>& deps,
                           std::function<void()> fn,
                           RunCondition cond) {
    SystemEntry entry;
    entry.name = name ? name : "";
    entry.phase = phase;
    entry.deps = deps;
    entry.fn = std::move(fn);
    entry.runCondition = std::move(cond);
    impl_->systemsForPhase(phase).push_back(std::move(entry));
}

// ---- Event management implementation ----

void App::registerEventUpdate(std::function<void()> fn) {
    impl_->eventUpdateFns.push_back(std::move(fn));
}

// ---- State management implementation ----

void App::flushCommands() {
    impl_->commands.flush(impl_->world);
}

void App::registerStateTransitionImpl(std::function<void()> processor) {
    impl_->stateTransitionProcessors.push_back(std::move(processor));
}

void App::registerInitialStateEnter(std::function<void()> fn) {
    impl_->initialStateEnters.push_back(std::move(fn));
}

void App::registerOnEnterFn(std::type_index type, int stateVal, const char* name,
                            const std::vector<AccessDescriptor>& deps,
                            std::function<void()> fn) {
    SystemEntry entry;
    entry.name = name ? name : "";
    entry.deps = deps;
    entry.fn = std::move(fn);
    impl_->onEnterSystems[{type, stateVal}].push_back(std::move(entry));
}

void App::registerOnEnterFn(std::type_index type, int stateVal, const char* name,
                            const std::vector<AccessDescriptor>& deps,
                            std::function<void(App&)> fn) {
    SystemEntry entry;
    entry.name = name ? name : "";
    entry.deps = deps;
    entry.lambdaFn = std::move(fn);
    entry.isLambda = true;
    impl_->onEnterSystems[{type, stateVal}].push_back(std::move(entry));
}

void App::registerOnExitFn(std::type_index type, int stateVal, const char* name,
                           const std::vector<AccessDescriptor>& deps,
                           std::function<void()> fn) {
    SystemEntry entry;
    entry.name = name ? name : "";
    entry.deps = deps;
    entry.fn = std::move(fn);
    impl_->onExitSystems[{type, stateVal}].push_back(std::move(entry));
}

void App::registerOnExitFn(std::type_index type, int stateVal, const char* name,
                           const std::vector<AccessDescriptor>& deps,
                           std::function<void(App&)> fn) {
    SystemEntry entry;
    entry.name = name ? name : "";
    entry.deps = deps;
    entry.lambdaFn = std::move(fn);
    entry.isLambda = true;
    impl_->onExitSystems[{type, stateVal}].push_back(std::move(entry));
}

void App::runOnEnterSystems(std::type_index type, int stateVal) {
    auto it = impl_->onEnterSystems.find({type, stateVal});
    if (it == impl_->onEnterSystems.end()) return;
    impl_->runSystems(it->second, *this);
}

void App::runOnExitSystems(std::type_index type, int stateVal) {
    auto it = impl_->onExitSystems.find({type, stateVal});
    if (it == impl_->onExitSystems.end()) return;
    impl_->runSystems(it->second, *this);
}

void App::processStateTransitions() {
    for (auto& processor : impl_->stateTransitionProcessors) {
        processor();
    }
}

// ---- End state management ----

World& App::world() {
    return impl_->world;
}

Commands& App::commands() {
    // If we're on a worker thread with a thread-local buffer, return that
    if (tl_commands) return *tl_commands;
    return impl_->commands;
}

int App::run() {
    if (!start()) return 1;
#ifdef __EMSCRIPTEN__
    // The browser drives frames; this call does not return.
    emscripten_set_main_loop_arg([](void* arg) {
        auto* app = static_cast<App*>(arg);
        if (!app->beginFrame()) {
            emscripten_cancel_main_loop();
            return;
        }
        app->endFrame();
    }, this, 0, true);
#else
    while (beginFrame()) {
        endFrame();
    }
#endif

    DRIFT_LOG_INFO("Drift engine shut down");
    return 0;
}

bool App::running() const {
    return impl_->running;
}

bool App::start() {
    auto& s = *impl_;
    if (s.started) return s.running;
    s.started = true;

    // ---- SDL init ----
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        DRIFT_LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    s.initialised = true;

    if (!createWindowAndDevice()) {
        return false;
    }
    int w = s.config.width > 0 ? s.config.width : 1280;
    int h = s.config.height > 0 ? s.config.height : 720;

    s.perfFrequency = SDL_GetPerformanceFrequency();
    s.lastCounter = SDL_GetPerformanceCounter();
    s.startCounter = s.lastCounter;

    DRIFT_LOG_INFO("Drift engine initialised (%dx%d)", w, h);

    // ---- Set up thread pool ----
    {
        int tc = s.config.threadCount;
#ifdef __EMSCRIPTEN__
        tc = 1;   // the web build has no threads
#endif
        if (tc <= 0) {
            int hw = static_cast<int>(std::thread::hardware_concurrency());
            tc = std::max(1, std::min(hw - 1, 7));
        }

        if (tc > 1) {
            // Create thread-safe allocator
            s.tsAllocator = std::make_unique<ThreadSafeAllocator>(s.world);

            // Create thread pool (tc-1 workers + main thread)
            s.threadPool = std::make_unique<ThreadPool>(tc - 1);

            // Create per-thread command buffers
            for (int i = 0; i < tc; ++i) {
                s.threadCommands.push_back(
                    std::make_unique<Commands>(*s.tsAllocator, s.world.componentRegistry(), &s.world));
            }

            DRIFT_LOG_INFO("Parallel scheduler: %d threads (%d workers + main)",
                           tc, tc - 1);
        } else {
            DRIFT_LOG_INFO("Parallel scheduler: sequential mode (1 thread)");
        }
    }

    // ---- Register core resources ----
    {
        auto* worldRes = new WorldResource(s.world);
        addResourceImpl(std::type_index(typeid(WorldResource)), worldRes);
    }
    {
        auto* snapshot = new RenderSnapshot();
        addResourceImpl(std::type_index(typeid(RenderSnapshot)), snapshot);
    }
    // Time resource
    s.timeRes = addResource<Time>();
    // AssetServer (empty, loaders registered by plugins)
    addResource<AssetServer>();

    // ---- Build plugins (deferred so window/GPU are available) ----
    for (auto* g : s.pluginGroups) {
        g->build(*this);
    }
    for (auto* p : s.plugins) {
        p->build(*this);
    }
    // ---- Finish plugins (all resources now available) ----
    for (auto* p : s.plugins) {
        p->finish(*this);
    }

    // ---- Sort systems by system set order ----
    auto sortBySet = [&](std::vector<SystemEntry>& systems, Phase phase) {
        auto it = s.setOrderByPhase.find(static_cast<int>(phase));
        if (it == s.setOrderByPhase.end()) return;
        const auto& order = it->second;
        // Build priority map: setId -> index in order vector
        std::unordered_map<SystemSetId, int> priority;
        for (int i = 0; i < static_cast<int>(order.size()); ++i) {
            priority[order[i]] = i;
        }
        std::stable_sort(systems.begin(), systems.end(),
            [&](const SystemEntry& a, const SystemEntry& b) {
                int pa = (a.systemSet >= 0 && priority.count(a.systemSet))
                    ? priority[a.systemSet] : static_cast<int>(order.size());
                int pb = (b.systemSet >= 0 && priority.count(b.systemSet))
                    ? priority[b.systemSet] : static_cast<int>(order.size());
                return pa < pb;
            });
    };
    sortBySet(s.preUpdateSystems, Phase::PreUpdate);
    sortBySet(s.updateSystems, Phase::Update);
    sortBySet(s.postUpdateSystems, Phase::PostUpdate);

    // ---- Run startup systems ----
    s.runSystems(s.startupSystems, *this);
    s.commands.flush(s.world);

    // ---- Run initial OnEnter callbacks ----
    for (auto& fn : s.initialStateEnters) {
        fn();
    }
    s.commands.flush(s.world);
    s.initialStateEnters.clear();

    s.running = true;
    return true;
}

bool App::beginFrame() {
    if (!pollFrame()) return false;
    simulate();
    return true;
}

bool App::pollFrame() {
    auto& s = *impl_;
    if (!s.running) return false;
    if (s.frameOpen) endFrame();
    // Delta time
    uint64_t now = SDL_GetPerformanceCounter();
    uint64_t elapsed = now - s.lastCounter;
    s.lastCounter = now;

    s.dt = static_cast<float>(
        static_cast<double>(elapsed) / static_cast<double>(s.perfFrequency));
    if (s.dt > 0.25f) s.dt = 0.25f;

    float instantFps = (s.dt > 0.f) ? (1.f / s.dt) : 0.f;
    const float smoothing = 0.9f;
    s.smoothedFps = (s.smoothedFps > 0.f)
        ? s.smoothedFps * smoothing + instantFps * (1.f - smoothing)
        : instantFps;
    s.fps = s.smoothedFps;

    // Update Time resource
    s.timeRes->delta = s.dt;
    s.timeRes->elapsed = static_cast<double>(now - s.startCounter) /
                       static_cast<double>(s.perfFrequency);
    s.timeRes->frame = s.frame;

    // Increment change detection tick
    s.world.setCurrentTick(static_cast<uint32_t>(s.frame + 1));

    // Swap event buffers at frame start (events written last frame become readable)
    for (auto& fn : s.eventUpdateFns) {
        fn();
    }

    // PreUpdate: beginFrame() snapshots previous state before new events
    uint64_t phaseT0 = SDL_GetPerformanceCounter();
    s.runSystemsParallel(s.preUpdateSystems, *this);
    s.commands.flush(s.world);
    s.preUpdateUs = (SDL_GetPerformanceCounter() - phaseT0) * 1000000 / s.perfFrequency;

    // Poll events (updates current state for this frame)
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        for (auto* handler : s.eventHandlers) {
            handler->processEvent(event);
        }

        if (event.type == SDL_EVENT_QUIT) {
            s.running = false;
        }
    }

    if (!s.running) return false;

    // State transitions (between event poll and Update phase)
    processStateTransitions();

    // Update and PostUpdate come next, from simulate() or simulateAsync().
    s.updateUs = 0;
    s.postUpdateUs = 0;
    s.simStage = 1;
    s.simProgress.clear();
    s.simTiming = {};
    s.frameOpen = true;
    return true;
}

void App::simulate() {
    auto& s = *impl_;
    if (s.simTiming.async) {
        finishSimulation();
        return;
    }
    if (s.simStage == 0) return;
    s.simTiming.start = SDL_GetPerformanceCounter();
    s.advanceSimulation(*this, false);
    s.simTiming.end = SDL_GetPerformanceCounter();
}

void App::simulateAsync() {
    auto& s = *impl_;
    if (s.simStage == 0) return;
#ifdef __EMSCRIPTEN__
    simulate();
#else
    if (s.config.threadCount == 1) {
        simulate();
        return;
    }
    s.waitSimThread();
    if (!s.simThread.joinable()) {
        s.simApp = this;
        s.simThread = std::thread([&s] { s.simLoop(); });
    }
    {
        std::lock_guard<std::mutex> lock(s.simMutex);
        s.simTiming.async = true;
        s.simRequested = true;
    }
    s.simCv.notify_all();
#endif
}

void App::finishSimulation() {
    auto& s = *impl_;
    s.waitSimThread();
    if (s.simStage == 0) return;
    uint64_t t0 = SDL_GetPerformanceCounter();
    s.advanceSimulation(*this, false);
    s.simTiming.deferredUs += (SDL_GetPerformanceCounter() - t0) * 1000000 / s.perfFrequency;
}

SimulationTiming App::lastSimulation() const { return impl_->simTiming; }

void App::endFrame() {
    auto& s = *impl_;
    if (!s.frameOpen) return;
    finishSimulation();
    s.frameOpen = false;

    // Extract phase: fill render snapshot (sequential - GPU ops)
    uint64_t phaseT0 = SDL_GetPerformanceCounter();
    s.runSystems(s.extractSystems, *this);
    uint64_t extractUs = (SDL_GetPerformanceCounter() - phaseT0) * 1000000 / s.perfFrequency;

    // Render phase: auto-render from snapshot + user manual draws (sequential)
    phaseT0 = SDL_GetPerformanceCounter();
    s.runSystems(s.renderSystems, *this);
    uint64_t renderUs = (SDL_GetPerformanceCounter() - phaseT0) * 1000000 / s.perfFrequency;

    // RenderFlush phase: endFrame + present (sequential)
    phaseT0 = SDL_GetPerformanceCounter();
    s.runSystems(s.renderFlushSystems, *this);
    uint64_t renderFlushUs = (SDL_GetPerformanceCounter() - phaseT0) * 1000000 / s.perfFrequency;

#ifdef DRIFT_PROFILE_SYSTEMS
    if (s.frame > 0 && s.frame % 10000 == 0) {
        DRIFT_LOG_INFO("=== Frame %llu profile (%.1f FPS, dt=%.2fms) ===",
                       (unsigned long long)s.frame, s.fps, s.dt * 1000.f);
        DRIFT_LOG_INFO("  Phases: PreUpdate=%lluus  Update=%lluus  PostUpdate=%lluus  Extract=%lluus  Render=%lluus  RenderFlush=%lluus",
                       (unsigned long long)s.preUpdateUs, (unsigned long long)s.updateUs,
                       (unsigned long long)s.postUpdateUs, (unsigned long long)extractUs,
                       (unsigned long long)renderUs, (unsigned long long)renderFlushUs);

        auto printSystems = [](const char* label, std::vector<SystemEntry>& systems) {
            for (auto& sys : systems) {
                if (sys.lastDurationUs > 0) {
                    DRIFT_LOG_INFO("    [%s] %s: %lluus", label, sys.name.c_str(),
                                   (unsigned long long)sys.lastDurationUs);
                }
            }
        };
        printSystems("PreUpdate", s.preUpdateSystems);
        printSystems("Update", s.updateSystems);
        printSystems("PostUpdate", s.postUpdateSystems);
        printSystems("Extract", s.extractSystems);
        printSystems("Render", s.renderSystems);
        printSystems("RenderFlush", s.renderFlushSystems);
    }
#endif

    s.frame++;
}

bool App::createWindowAndDevice() {
    auto& s = *impl_;
    gfx::registerBuiltinBackends();
    const auto& available = gfx::backends();
    if (available.empty()) {
        DRIFT_LOG_ERROR("No graphics backend compiled in");
        return false;
    }

    std::string wanted = s.config.renderer;
    if (const char* env = SDL_getenv("DRIFT_RENDERER")) wanted = env;

    // Requested backend first, then the rest in registration order.
    std::vector<const gfx::BackendInfo*> order;
    for (const auto& b : available) {
        if (wanted == b.name) order.push_back(&b);
    }
    if (!wanted.empty() && order.empty()) {
        DRIFT_LOG_WARN("Renderer '%s' is not available in this build", wanted.c_str());
    }
    for (const auto& b : available) {
        if (wanted != b.name) order.push_back(&b);
    }

    const char* title = s.config.title.empty() ? "Drift Engine" : s.config.title.c_str();
    int w = s.config.width > 0 ? s.config.width : 1280;
    int h = s.config.height > 0 ? s.config.height : 720;

    for (const gfx::BackendInfo* b : order) {
        b->prepareWindow(s.config);
        SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(b->windowFlags());
        if (s.config.fullscreen) flags |= SDL_WINDOW_FULLSCREEN;
        if (s.config.resizable) flags |= SDL_WINDOW_RESIZABLE;
        if (s.config.highDpi) flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;

        s.window = SDL_CreateWindow(title, w, h, flags);
        if (!s.window) {
            DRIFT_LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
            return false;
        }
        s.device = b->create(s.window, s.config);
        if (s.device) {
            DRIFT_LOG_INFO("Graphics backend: %s", b->name);
            return true;
        }
        DRIFT_LOG_WARN("Graphics backend '%s' failed to start", b->name);
        SDL_DestroyWindow(s.window);
        s.window = nullptr;
    }
    DRIFT_LOG_ERROR("No graphics backend could start");
    return false;
}

void App::quit() {
    impl_->running = false;
}

const Config& App::config() const { return impl_->config; }

std::vector<SystemTiming> App::systemTimings() const {
    std::vector<SystemTiming> out;
    const Phase phases[] = {Phase::PreUpdate, Phase::Update, Phase::PostUpdate,
                            Phase::Extract, Phase::Render, Phase::RenderFlush};
    for (Phase p : phases) {
        for (const auto& sys : impl_->systemsForPhase(p)) {
            out.push_back(SystemTiming{sys.name, p, sys.lastDurationUs});
        }
    }
    return out;
}
void* App::sdlWindow() const { return impl_->window; }
gfx::Device* App::graphicsDevice() const { return impl_->device.get(); }

// ---- Ordering constraints (Phase 5) ----

size_t App::lastSystemIndex(Phase phase) {
    auto& systems = impl_->systemsForPhase(phase);
    return systems.empty() ? 0 : systems.size() - 1;
}

void App::addSystemOrderAfter(Phase phase, size_t index, const char* depName) {
    auto& systems = impl_->systemsForPhase(phase);
    if (index < systems.size() && depName) {
        systems[index].mustRunAfter.emplace_back(depName);
    }
}

void App::addSystemOrderBefore(Phase phase, size_t index, const char* targetName) {
    auto& systems = impl_->systemsForPhase(phase);
    if (index < systems.size() && targetName) {
        systems[index].mustRunBefore.emplace_back(targetName);
    }
}

void App::setSystemSet(Phase phase, size_t index, SystemSetId setId) {
    auto& systems = impl_->systemsForPhase(phase);
    if (index < systems.size()) {
        systems[index].systemSet = setId;
    }
}

void App::configureSetOrder(Phase phase, const std::vector<SystemSetId>& order) {
    impl_->setOrderByPhase[static_cast<int>(phase)] = order;
}

} // namespace drift
