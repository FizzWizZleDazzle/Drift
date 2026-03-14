#include <drift/App.hpp>
#include <drift/Log.hpp>
#include <drift/World.hpp>
#include <drift/Commands.h>
#include <drift/WorldResource.h>
#include <drift/RenderSnapshot.h>
#include <drift/AssetServer.h>
#include <drift/resources/Time.hpp>

#include <SDL3/SDL.h>

#include <unordered_map>
#include <string>
#include <typeindex>
#include <vector>
#include <algorithm>

namespace drift {

// ---- System entry stored in the scheduler ----
struct SystemEntry {
    std::string name;
    Phase phase;
    std::vector<AccessDescriptor> deps;
    std::function<void()> fn;             // typed system function
    std::function<void(App&)> lambdaFn;   // lambda shorthand
    SystemBase* rawSystem = nullptr;      // SWIG system (owned)
    bool isLambda = false;
    bool isRaw = false;
};

struct App::Impl {
    Config config;

    SDL_Window* window = nullptr;
    SDL_GPUDevice* gpuDevice = nullptr;

    bool running = false;
    bool initialised = false;

    // ECS World + Commands (owned by App)
    World world;
    Commands commands{world, world.componentRegistry()};

    // Frame timing
    uint64_t lastCounter = 0;
    uint64_t perfFrequency = 0;
    float dt = 0.f;
    float fps = 0.f;
    float smoothedFps = 0.f;
    uint64_t frame = 0;
    uint64_t startCounter = 0;

    // Resources: dual storage for C++ fast path + SWIG name path
    std::unordered_map<std::type_index, Resource*> resourcesByType;
    std::unordered_map<std::string, Resource*> resourcesByName;
    std::vector<Resource*> ownedResources;

    // Systems grouped by phase
    std::vector<SystemEntry> startupSystems;
    std::vector<SystemEntry> preUpdateSystems;
    std::vector<SystemEntry> updateSystems;
    std::vector<SystemEntry> postUpdateSystems;
    std::vector<SystemEntry> extractSystems;
    std::vector<SystemEntry> renderSystems;
    std::vector<SystemEntry> renderFlushSystems;

    // Event handlers
    std::vector<EventHandler*> eventHandlers;

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

    void runSystems(std::vector<SystemEntry>& systems, App& app) {
        for (auto& sys : systems) {
            if (sys.isRaw && sys.rawSystem) {
                sys.rawSystem->execute(app, dt);
            } else if (sys.isLambda) {
                sys.lambdaFn(app);
            } else if (sys.fn) {
                sys.fn();
            }
        }
    }
};

App::App() : impl_(std::make_unique<Impl>()) {}

App::~App() {
    // Clean up resources
    for (auto* r : impl_->ownedResources) {
        delete r;
    }
    // Clean up plugins
    for (auto* p : impl_->plugins) delete p;
    for (auto* g : impl_->pluginGroups) delete g;
    // Clean up raw systems
    auto cleanSystems = [](std::vector<SystemEntry>& systems) {
        for (auto& s : systems) {
            if (s.isRaw) delete s.rawSystem;
        }
    };
    cleanSystems(impl_->startupSystems);
    cleanSystems(impl_->preUpdateSystems);
    cleanSystems(impl_->updateSystems);
    cleanSystems(impl_->postUpdateSystems);
    cleanSystems(impl_->extractSystems);
    cleanSystems(impl_->renderSystems);
    cleanSystems(impl_->renderFlushSystems);

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

void App::addResourceByName(const char* name, Resource* res) {
    if (!name || !res) return;
    impl_->resourcesByName[name] = res;
    impl_->ownedResources.push_back(res);
}

Resource* App::getResourceByName(const char* name) {
    if (!name) return nullptr;
    auto it = impl_->resourcesByName.find(name);
    return it != impl_->resourcesByName.end() ? it->second : nullptr;
}

void App::addSystemRaw(const char* name, Phase phase, SystemBase* sys) {
    if (!sys) return;
    SystemEntry entry;
    entry.name = name ? name : "";
    entry.phase = phase;
    entry.rawSystem = sys;
    entry.isRaw = true;
    if (sys) entry.deps = sys->getDependencies();
    impl_->systemsForPhase(phase).push_back(std::move(entry));
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
    const char* n = res->name();
    if (n && n[0] != '\0') {
        impl_->resourcesByName[n] = res;
    }
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

World& App::world() {
    return impl_->world;
}

Commands& App::commands() {
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

    const char* title = s.config.title ? s.config.title : "Drift Engine";
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

    // ---- Run startup systems ----
    s.runSystems(s.startupSystems, *this);
    s.commands.flush(s.world);

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

        // PreUpdate: beginFrame() snapshots previous state before new events
        s.runSystems(s.preUpdateSystems, *this);
        s.commands.flush(s.world);

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

        // Update phase
        s.runSystems(s.updateSystems, *this);
        s.commands.flush(s.world);

        // PostUpdate phase
        s.runSystems(s.postUpdateSystems, *this);
        s.commands.flush(s.world);

        // Extract phase: fill render snapshot
        s.runSystems(s.extractSystems, *this);

        // Render phase: auto-render from snapshot + user manual draws
        s.runSystems(s.renderSystems, *this);

        // RenderFlush phase: endFrame + present
        s.runSystems(s.renderFlushSystems, *this);

        s.frame++;
    }

    DRIFT_LOG_INFO("Drift engine shut down");
    return 0;
}

void App::quit() {
    impl_->running = false;
}

float App::deltaTime() const { return impl_->dt; }

double App::time() const {
    if (!impl_->initialised || impl_->perfFrequency == 0) return 0.0;
    uint64_t now = SDL_GetPerformanceCounter();
    return static_cast<double>(now - impl_->startCounter) /
           static_cast<double>(impl_->perfFrequency);
}

uint64_t App::frameCount() const { return impl_->frame; }
const Config& App::config() const { return impl_->config; }
void* App::sdlWindow() const { return impl_->window; }
void* App::gpuDevice() const { return impl_->gpuDevice; }

} // namespace drift
