#pragma once

#include <drift/Types.hpp>
#include <drift/Config.hpp>
#include <drift/Settings.hpp>
#include <drift/Handle.hpp>
#include <drift/Resource.hpp>
#include <drift/Plugin.hpp>
#include <drift/System.hpp>
#include <drift/World.hpp>
#include <drift/State.hpp>

#include <memory>
#include <functional>
#include <string>
#include <typeindex>
#include <vector>

union SDL_Event;

namespace drift {

namespace gfx { class Device; }

class Commands;

// EventHandler: receives raw SDL events (for InputResource, UIResource, etc.)
class EventHandler {
public:
    virtual ~EventHandler() = default;
    virtual void processEvent(const SDL_Event& event) = 0;
};

class App;

using SystemSetId = int;

// How long a system took the last time it ran.
struct SystemTiming {
    std::string name;
    Phase phase;
    uint64_t microseconds = 0;
};

// When the last frame's simulation (Update and PostUpdate) ran, on the
// SDL performance-counter clock (SDL_GetPerformanceCounter ticks).
// `start`/`end` bracket the part that ran in simulate() or on the
// background thread of simulateAsync(); `deferredUs` is the time
// finishSimulation() spent on systems that had to wait for the main thread.
struct SimulationTiming {
    uint64_t start = 0;
    uint64_t end = 0;
    uint64_t deferredUs = 0;
    bool async = false;
};

// Handle returned by phase shortcuts for chaining .before()/.after() ordering
class SystemHandle {
public:
    SystemHandle(App& app, size_t index, Phase phase)
        : app_(app), index_(index), phase_(phase) {}

    SystemHandle& before(const char* name);
    SystemHandle& after(const char* name);
    SystemHandle& inSet(SystemSetId setId);

private:
    App& app_;
    size_t index_;
    Phase phase_;
};

class App {
public:
    App();
    ~App();

    // ---- Settings ----
    // Settings come from defaults, then drift.toml, then code, then
    // DRIFT_<SECTION>_<KEY> environment variables and --set flags.
    //
    // Reads --config <path> and --set section.key=value. Call it first, so
    // the file it names is the one read.
    App& args(int argc, char** argv);
    // The engine's settings. Change fields before run() to override
    // drift.toml for this game; environment and --set values still win.
    Config& config();
    // Every drift.toml value, by path, and the bindings below.
    Settings& settingsStore();
    // Binds [section] of drift.toml to resource T and returns it, adding it
    // on first use. T describes its fields with a static
    // describe(reflect::TypeBuilder<T>&); see Settings.hpp.
    template<typename T>
    T& settings(const char* section, SettingsOptions options = {}) {
        T* res = getResource<T>();
        if (!res) {
            reflect::TypeBuilder<T> b = reflect::type<T>(section);
            T::describe(b);
            res = addResource<T>();
        }
        settingsStore().bind(section, *reflect::typeOf<T>(), res, std::move(options));
        return *res;
    }
    // Runs `fn` after a reload of drift.toml changed keys of [section].
    void onSettingsChanged(const char* section, std::function<void(const std::vector<std::string>&)> fn);

    // Builder
    App& addPlugin(Plugin* plugin);
    App& addPlugins(PluginGroup* group);
    void addEventHandler(EventHandler* handler);

    // Runs the app until it quits: start(), then beginFrame()/endFrame()
    // until beginFrame() returns false. Returns 0, or 1 if start() failed.
    int run();
    void quit();

    // ---- Driving the loop yourself ----
    // For a host that owns its own loop (an embedding program or a language
    // binding) instead of handing it to run(). Call start() once, then per
    // frame beginFrame(), your own work, endFrame(). Between the two halves
    // the world and every resource are safe to read and change from the
    // calling thread: no system is running, input reflects this frame's
    // events, physics has stepped, and draws queued on RendererResource are
    // rendered by endFrame().
    //
    // Opens the window and graphics device, builds plugins and runs
    // Startup systems. Returns false on failure. Only the first call does
    // anything.
    bool start();
    // Advances time, polls events and runs PreUpdate, Update and PostUpdate.
    // Returns false once the app has quit (window closed or quit() called);
    // do not call endFrame() then. The same as pollFrame() then simulate().
    bool beginFrame();
    // Runs Extract, Render and RenderFlush: draws and presents the frame.
    // Finishes a simulation still in flight first.
    void endFrame();
    bool running() const;

    // ---- Stepping the frame in parts ----
    // beginFrame() is these two calls. A host that has its own work to do
    // each frame can call them separately and run the simulation on a
    // background thread while it works.
    //
    // Advances time, polls events and runs PreUpdate and state transitions.
    // Returns false once the app has quit.
    bool pollFrame();
    // Runs Update and PostUpdate on the calling thread (with the worker
    // pool, as beginFrame() does).
    void simulate();
    // Starts Update and PostUpdate on a background thread and returns at
    // once. Until finishSimulation() the caller must not touch the world,
    // Commands, or any resource a system may use; resources that are
    // mainThreadOnly() are the exception, because systems that use them do
    // not run on the background thread. Those systems, and systems that
    // conflict with them, wait for finishSimulation(), which runs them on
    // the calling thread; they keep their order among themselves and
    // against conflicting systems, but exclusive (lambda) systems registered
    // after them run first. Exclusive systems run on the background thread,
    // so they must not use thread-bound state. Without threads (the web
    // build, or config().app.threads = 1) this is simulate().
    void simulateAsync();
    // Waits for simulateAsync() and runs the systems it left for the main
    // thread. Does nothing when no simulation is in flight.
    void finishSimulation();
    // When the last simulation ran; see SimulationTiming.
    SimulationTiming lastSimulation() const;

    const Config& config() const;

    // Per-system timings from the scheduler, in phase and run order.
    std::vector<SystemTiming> systemTimings() const;

    // World + Commands access
    World& world();
    Commands& commands();

    // Internal: get window/GPU device for subsystems
    void* sdlWindow() const;
    gfx::Device* graphicsDevice() const;

    // Template API
    template<typename T, typename... Args>
    T* addResource(Args&&... args) {
        T* res = new T(std::forward<Args>(args)...);
        addResourceImpl(std::type_index(typeid(T)), res);
        return res;
    }

    template<typename T, typename... Args>
    T* initResource(Args&&... args) {
        T* existing = getResource<T>();
        if (existing) return existing;
        return addResource<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    T* getResource() {
        return static_cast<T*>(getResourceImpl(std::type_index(typeid(T))));
    }

    template<typename T>
    const T* getResource() const {
        return static_cast<const T*>(getResourceImpl(std::type_index(typeid(T))));
    }

    template<typename T>
    App& addPlugin() {
        T* p = new T();
        return addPlugin(p);
    }

    template<typename T>
    App& addPlugins() {
        T* g = new T();
        return addPlugins(g);
    }

    // Phase shortcuts (return SystemHandle for .before()/.after() chaining)
    template<auto Fn> SystemHandle startup(const char* n = "startup") { return addSystem<Fn>(n, Phase::Startup); }
    template<auto Fn> SystemHandle update(const char* n = "update")   { return addSystem<Fn>(n, Phase::Update); }
    template<auto Fn> SystemHandle render(const char* n = "render")   { return addSystem<Fn>(n, Phase::Render); }
    template<auto Fn> SystemHandle preUpdate(const char* n = "preUpdate")   { return addSystem<Fn>(n, Phase::PreUpdate); }
    template<auto Fn> SystemHandle postUpdate(const char* n = "postUpdate") { return addSystem<Fn>(n, Phase::PostUpdate); }

    // Phase shortcuts with RunCondition
    template<auto Fn> SystemHandle update(const char* n, RunCondition cond)   { return addSystem<Fn>(n, Phase::Update, std::move(cond)); }
    template<auto Fn> SystemHandle render(const char* n, RunCondition cond)   { return addSystem<Fn>(n, Phase::Render, std::move(cond)); }
    template<auto Fn> SystemHandle preUpdate(const char* n, RunCondition cond)   { return addSystem<Fn>(n, Phase::PreUpdate, std::move(cond)); }
    template<auto Fn> SystemHandle postUpdate(const char* n, RunCondition cond) { return addSystem<Fn>(n, Phase::PostUpdate, std::move(cond)); }

    // Bevy-style: system function with typed params
    template<auto Fn>
    SystemHandle addSystem(const char* name, Phase phase) {
        using Traits = SystemTraits<decltype(Fn)>;
        auto deps = Traits::dependencies();
        registerSystemFn(name, phase, deps, [this]() {
            invokeSystem<Fn>();
        });
        return SystemHandle(*this, lastSystemIndex(phase), phase);
    }

    // Bevy-style with RunCondition
    template<auto Fn>
    SystemHandle addSystem(const char* name, Phase phase, RunCondition cond) {
        using Traits = SystemTraits<decltype(Fn)>;
        auto deps = Traits::dependencies();
        registerSystemFn(name, phase, deps, [this]() {
            invokeSystem<Fn>();
        }, std::move(cond));
        return SystemHandle(*this, lastSystemIndex(phase), phase);
    }

    // Batch system registration
    template<auto Fn>
    struct System { const char* name; };

    template<auto Fn, auto... Rest>
    void addSystems(Phase phase, System<Fn> first, System<Rest>... rest) {
        addSystem<Fn>(first.name, phase);
        if constexpr (sizeof...(Rest) > 0) {
            addSystems(phase, rest...);
        }
    }

    // Lambda shorthand (for plugins registering internal systems)
    void addSystem(const char* name, Phase phase, std::function<void(App&)> fn);

    // ---- Event management ----

    // Pre-register an event type (creates Events<T> resource + registers update callback)
    template<typename T>
    void initEvent() {
        if (!getResource<Events<T>>()) {
            addResource<Events<T>>();
            registerEventUpdate([this]() {
                auto* events = getResource<Events<T>>();
                if (events) events->update();
            });
        }
    }

    // ---- State management ----

    // Initialize a state enum type with an initial value
    template<typename T>
    void initState(T initial) {
        addResource<State<T>>(initial);
        addResource<NextState<T>>();
        int initialVal = static_cast<int>(initial);
        auto typeIdx = std::type_index(typeid(T));

        // Register transition processor for this state type
        registerStateTransitionImpl([this, typeIdx]() {
            processStateTransitionForType<T>(typeIdx);
        });

        // Register initial OnEnter callback
        registerInitialStateEnter([this, initialVal, typeIdx]() {
            runOnEnterSystems(typeIdx, initialVal);
        });
    }

    // Register OnEnter system (typed function)
    template<auto Fn, typename T>
    void onEnter(T state, const char* name) {
        using Traits = SystemTraits<decltype(Fn)>;
        auto deps = Traits::dependencies();
        registerOnEnterFn(std::type_index(typeid(T)), static_cast<int>(state),
                          name, deps, [this]() { invokeSystem<Fn>(); });
    }

    // Register OnEnter system (lambda)
    template<typename T>
    void onEnter(T state, const char* name, std::function<void(App&)> fn) {
        registerOnEnterFn(std::type_index(typeid(T)), static_cast<int>(state),
                          name, {}, std::move(fn));
    }

    // Register OnExit system (typed function)
    template<auto Fn, typename T>
    void onExit(T state, const char* name) {
        using Traits = SystemTraits<decltype(Fn)>;
        auto deps = Traits::dependencies();
        registerOnExitFn(std::type_index(typeid(T)), static_cast<int>(state),
                         name, deps, [this]() { invokeSystem<Fn>(); });
    }

    // Register OnExit system (lambda)
    template<typename T>
    void onExit(T state, const char* name, std::function<void(App&)> fn) {
        registerOnExitFn(std::type_index(typeid(T)), static_cast<int>(state),
                         name, {}, std::move(fn));
    }

    // System set ordering
    void configureSetOrder(Phase phase, const std::vector<SystemSetId>& order);

    template<typename T>
    void configureSets(Phase phase, std::initializer_list<T> sets) {
        std::vector<SystemSetId> order;
        for (auto s : sets) order.push_back(static_cast<SystemSetId>(s));
        configureSetOrder(phase, order);
    }

    // Ordering internals (used by SystemHandle)
    friend class SystemHandle;
    size_t lastSystemIndex(Phase phase);
    void addSystemOrderAfter(Phase phase, size_t index, const char* depName);
    void addSystemOrderBefore(Phase phase, size_t index, const char* targetName);
    void setSystemSet(Phase phase, size_t index, SystemSetId setId);

private:
    bool createWindowAndDevice();
    void addResourceImpl(std::type_index type, Resource* res);
    Resource* getResourceImpl(std::type_index type) const;
    void registerSystemFn(const char* name, Phase phase,
                          const std::vector<AccessDescriptor>& deps,
                          std::function<void()> fn);
    void registerSystemFn(const char* name, Phase phase,
                          const std::vector<AccessDescriptor>& deps,
                          std::function<void()> fn,
                          RunCondition cond);

    // Event internals
    void registerEventUpdate(std::function<void()> fn);

    // State management internals
    void flushCommands();
    void registerStateTransitionImpl(std::function<void()> processor);
    void registerInitialStateEnter(std::function<void()> fn);
    void registerOnEnterFn(std::type_index type, int stateVal, const char* name,
                           const std::vector<AccessDescriptor>& deps,
                           std::function<void()> fn);
    void registerOnEnterFn(std::type_index type, int stateVal, const char* name,
                           const std::vector<AccessDescriptor>& deps,
                           std::function<void(App&)> fn);
    void registerOnExitFn(std::type_index type, int stateVal, const char* name,
                          const std::vector<AccessDescriptor>& deps,
                          std::function<void()> fn);
    void registerOnExitFn(std::type_index type, int stateVal, const char* name,
                          const std::vector<AccessDescriptor>& deps,
                          std::function<void(App&)> fn);
    void runOnEnterSystems(std::type_index type, int stateVal);
    void runOnExitSystems(std::type_index type, int stateVal);
    void processStateTransitions();

    template<typename T>
    void processStateTransitionForType(std::type_index typeIdx) {
        auto* next = getResource<NextState<T>>();
        if (!next || !next->hasPending()) return;

        auto* state = getResource<State<T>>();
        int oldVal = static_cast<int>(state->current);
        T newState = next->take();
        int newVal = static_cast<int>(newState);

        runOnExitSystems(typeIdx, oldVal);
        flushCommands();

        state->current = newState;

        runOnEnterSystems(typeIdx, newVal);
        flushCommands();
    }

    // Invoke a typed system function by constructing Res/ResMut wrappers
    template<auto Fn, typename... Args>
    void invokeSystemHelper(void(*)(Args...)) {
        Fn(buildParam<Args>()...);
    }

    template<auto Fn>
    void invokeSystem() {
        invokeSystemHelper<Fn>(Fn);
    }

    template<typename T>
    auto buildParam() -> std::enable_if_t<IsRes<T>::value, T> {
        using Inner = typename InnerType<T>::type;
        return T(getResource<Inner>());
    }

    template<typename T>
    auto buildParam() -> std::enable_if_t<IsResMut<T>::value, T> {
        using Inner = typename InnerType<T>::type;
        return T(getResource<Inner>());
    }

    template<typename T>
    auto buildParam() -> std::enable_if_t<std::is_same_v<T, Commands&>, Commands&> {
        return commands();
    }

    // Query/QueryMut parameter building
    template<typename T>
    auto buildParam() -> std::enable_if_t<IsQuery<T>::value, T> {
        return T(world(), world().componentRegistry());
    }

    template<typename T>
    auto buildParam() -> std::enable_if_t<IsQueryMut<T>::value, T> {
        return T(world(), world().componentRegistry());
    }

    // EventWriter<T> parameter building (auto-registers Events<T> on first use)
    template<typename T>
    auto buildParam() -> std::enable_if_t<IsEventWriter<T>::value, T> {
        using Inner = typename EventInnerType<T>::type;
        initEvent<Inner>();
        return T(getResource<Events<Inner>>());
    }

    // EventReader<T> parameter building (auto-registers Events<T> on first use)
    template<typename T>
    auto buildParam() -> std::enable_if_t<IsEventReader<T>::value, T> {
        using Inner = typename EventInnerType<T>::type;
        initEvent<Inner>();
        return T(getResource<Events<Inner>>());
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// SystemHandle inline implementations
inline SystemHandle& SystemHandle::before(const char* name) {
    app_.addSystemOrderBefore(phase_, index_, name);
    return *this;
}

inline SystemHandle& SystemHandle::after(const char* name) {
    app_.addSystemOrderAfter(phase_, index_, name);
    return *this;
}

inline SystemHandle& SystemHandle::inSet(SystemSetId setId) {
    app_.setSystemSet(phase_, index_, setId);
    return *this;
}

// Free function: returns a RunCondition that checks if State<T>::current == value
template<typename T>
RunCondition inState(T value) {
    return [value](const App& app) -> bool {
        auto* state = app.getResource<State<T>>();
        return state && state->current == value;
    };
}

} // namespace drift
