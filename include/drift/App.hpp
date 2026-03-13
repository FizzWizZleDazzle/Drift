#pragma once

#include <drift/Types.hpp>
#include <drift/Config.hpp>
#include <drift/Handle.hpp>
#include <drift/Resource.hpp>
#include <drift/Plugin.hpp>
#include <drift/System.hpp>

#include <memory>
#include <functional>
#include <string>
#include <typeindex>

union SDL_Event;

namespace drift {

class World;

// EventHandler: receives raw SDL events (for InputResource, UIResource, etc.)
class EventHandler {
public:
    virtual ~EventHandler() = default;
    virtual void processEvent(const SDL_Event& event) = 0;
};

class App {
public:
    App();
    ~App();

    // Builder
    App& setConfig(const Config& cfg);
    App& addPlugin(Plugin* plugin);       // SWIG-visible: takes ownership
    App& addPlugins(PluginGroup* group);  // SWIG-visible: takes ownership
    void addResourceByName(const char* name, Resource* res);  // SWIG-visible: takes ownership
    Resource* getResourceByName(const char* name);
    void addSystemRaw(const char* name, Phase phase, SystemBase* sys); // SWIG-visible
    void addEventHandler(EventHandler* handler);

    int run();
    void quit();

    float deltaTime() const;
    double time() const;
    uint64_t frameCount() const;
    const Config& config() const;

    // Internal: get window/GPU device for subsystems
    void* sdlWindow() const;
    void* gpuDevice() const;

#ifndef SWIG
    // C++ template API
    template<typename T, typename... Args>
    T* addResource(Args&&... args) {
        T* res = new T(std::forward<Args>(args)...);
        addResourceImpl(std::type_index(typeid(T)), res);
        return res;
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

    // Phase shortcuts
    template<auto Fn> void startup(const char* n = "startup") { addSystem<Fn>(n, Phase::Startup); }
    template<auto Fn> void update(const char* n = "update")   { addSystem<Fn>(n, Phase::Update); }
    template<auto Fn> void render(const char* n = "render")   { addSystem<Fn>(n, Phase::Render); }
    template<auto Fn> void preUpdate(const char* n = "preUpdate")   { addSystem<Fn>(n, Phase::PreUpdate); }
    template<auto Fn> void postUpdate(const char* n = "postUpdate") { addSystem<Fn>(n, Phase::PostUpdate); }

    // Bevy-style: system function with typed params
    template<auto Fn>
    void addSystem(const char* name, Phase phase) {
        using Traits = SystemTraits<decltype(Fn)>;
        auto deps = Traits::dependencies();
        registerSystemFn(name, phase, deps, [this](float dt) {
            invokeSystem<Fn>(dt);
        });
    }

    // Lambda shorthand (for plugins registering internal systems)
    void addSystem(const char* name, Phase phase, std::function<void(App&, float)> fn);

private:
    void addResourceImpl(std::type_index type, Resource* res);
    Resource* getResourceImpl(std::type_index type) const;
    void registerSystemFn(const char* name, Phase phase,
                          const std::vector<AccessDescriptor>& deps,
                          std::function<void(float)> fn);

    // Invoke a typed system function by constructing Res/ResMut wrappers
    template<auto Fn, typename... Args>
    void invokeSystemHelper(float dt, void(*)(Args...)) {
        Fn(buildParam<Args>(dt)...);
    }

    template<auto Fn>
    void invokeSystem(float dt) {
        invokeSystemHelper<Fn>(dt, Fn);
    }

    template<typename T>
    auto buildParam(float dt) -> std::enable_if_t<std::is_same_v<T, float>, float> {
        return dt;
    }

    template<typename T>
    auto buildParam(float) -> std::enable_if_t<IsRes<T>::value, T> {
        using Inner = typename InnerType<T>::type;
        return T(getResource<Inner>());
    }

    template<typename T>
    auto buildParam(float) -> std::enable_if_t<IsResMut<T>::value, T> {
        using Inner = typename InnerType<T>::type;
        return T(getResource<Inner>());
    }
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace drift
