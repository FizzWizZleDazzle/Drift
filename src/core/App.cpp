#include <drift/App.hpp>
#include <drift/Log.hpp>
#include <drift/World.hpp>
#include <drift/Commands.hpp>
#include <drift/WorldResource.h>
#include <drift/RenderSnapshot.hpp>
#include <drift/AssetServer.hpp>
#include <drift/resources/Time.hpp>

#include <SDL3/SDL.h>

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
                DRIFT_LOG_ERROR("ThreadPool: possible deadlock — remaining=%d, batchGen=%llu, tasks=%d",
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
};

// Key for onEnter/onExit maps: (type_index of enum, int-cast enum value)
using StateKey = std::pair<std::type_index, int>;

struct App::Impl {
    Config config;

    SDL_Window* window = nullptr;
    SDL_GPUDevice* gpuDevice = nullptr;

    bool running = false;
    bool initialised = false;

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

    void runSystemsParallel(std::vector<SystemEntry>& systems, App& app) {
        if (systems.empty()) return;

        // If no thread pool or only 1 thread, fall back to sequential
        if (!threadPool || threadPool->threadCount() <= 1) {
            runSystems(systems, app);
            return;
        }

        int n = static_cast<int>(systems.size());
        std::vector<bool> completed(n, false);
        int completedCount = 0;

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
            }

            if (batch.empty()) break;  // Safety: prevent infinite loop

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
    }
};

App::App() : impl_(std::make_unique<Impl>()) {}

App::~App() {
    // Destroy thread pool first (joins threads before any resources are freed)
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
    if (impl_->gpuDevice) {
        if (impl_->window)
            SDL_ReleaseWindowFromGPUDevice(impl_->gpuDevice, impl_->window);
        SDL_DestroyGPUDevice(impl_->gpuDevice);
    }
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
    auto& s = *impl_;

    // ---- SDL init ----
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        DRIFT_LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }
    s.initialised = true;

    const char* title = s.config.title.empty() ? "Drift Engine" : s.config.title.c_str();
    int w = s.config.width > 0 ? s.config.width : 1280;
    int h = s.config.height > 0 ? s.config.height : 720;

    SDL_WindowFlags windowFlags = 0;
    if (s.config.fullscreen) windowFlags |= SDL_WINDOW_FULLSCREEN;
    if (s.config.resizable) windowFlags |= SDL_WINDOW_RESIZABLE;

    s.window = SDL_CreateWindow(title, w, h, windowFlags);
    if (!s.window) {
        DRIFT_LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        return 1;
    }

    s.gpuDevice = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL,
        true, nullptr);
    if (!s.gpuDevice) {
        DRIFT_LOG_ERROR("SDL_CreateGPUDevice failed: %s", SDL_GetError());
        return 1;
    }

    if (!SDL_ClaimWindowForGPUDevice(s.gpuDevice, s.window)) {
        DRIFT_LOG_ERROR("SDL_ClaimWindowForGPUDevice failed: %s", SDL_GetError());
        return 1;
    }

    SDL_SetGPUSwapchainParameters(s.gpuDevice, s.window,
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
        s.config.vsync ? SDL_GPU_PRESENTMODE_VSYNC : SDL_GPU_PRESENTMODE_IMMEDIATE);

    s.perfFrequency = SDL_GetPerformanceFrequency();
    s.lastCounter = SDL_GetPerformanceCounter();
    s.startCounter = s.lastCounter;

    DRIFT_LOG_INFO("Drift engine initialised (%dx%d)", w, h);

    // ---- Set up thread pool ----
    {
        int tc = s.config.threadCount;
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
    auto* timeRes = addResource<Time>();
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

    // ---- Main loop ----
    s.running = true;
    while (s.running) {
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
        timeRes->delta = s.dt;
        timeRes->elapsed = static_cast<double>(now - s.startCounter) /
                           static_cast<double>(s.perfFrequency);
        timeRes->frame = s.frame;

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
        uint64_t preUpdateUs = (SDL_GetPerformanceCounter() - phaseT0) * 1000000 / s.perfFrequency;

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

        if (!s.running) break;

        // State transitions (between event poll and Update phase)
        processStateTransitions();

        // Update phase
        phaseT0 = SDL_GetPerformanceCounter();
        s.runSystemsParallel(s.updateSystems, *this);
        s.commands.flush(s.world);
        uint64_t updateUs = (SDL_GetPerformanceCounter() - phaseT0) * 1000000 / s.perfFrequency;

        // PostUpdate phase
        phaseT0 = SDL_GetPerformanceCounter();
        s.runSystemsParallel(s.postUpdateSystems, *this);
        s.commands.flush(s.world);
        uint64_t postUpdateUs = (SDL_GetPerformanceCounter() - phaseT0) * 1000000 / s.perfFrequency;

        // Extract phase: fill render snapshot (sequential - GPU ops)
        phaseT0 = SDL_GetPerformanceCounter();
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
                           (unsigned long long)preUpdateUs, (unsigned long long)updateUs,
                           (unsigned long long)postUpdateUs, (unsigned long long)extractUs,
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

    DRIFT_LOG_INFO("Drift engine shut down");
    return 0;
}

void App::quit() {
    impl_->running = false;
}

const Config& App::config() const { return impl_->config; }
void* App::sdlWindow() const { return impl_->window; }
void* App::gpuDevice() const { return impl_->gpuDevice; }

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
