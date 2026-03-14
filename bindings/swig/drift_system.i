// =============================================================================
// drift_system.i - System, Plugin, Script, and EventHandler director bindings
// =============================================================================
// Enables C# subclassing of Plugin, PluginGroup, SystemBase, Script, and
// EventHandler via SWIG directors. This is the core mechanism that allows
// game logic to be written in C# while the engine runs in native C++.
// =============================================================================

%{
#include <drift/Resource.hpp>
#include <drift/Plugin.hpp>
#include <drift/System.hpp>
#include <drift/App.hpp>
#include <drift/World.hpp>
#include <drift/Entity.hpp>
#include <drift/Script.hpp>
#include <drift/Log.hpp>
#include <drift/Commands.h>
#include <drift/RenderSnapshot.h>
#include <drift/WorldResource.h>
#include <drift/AssetServer.h>
#include <drift/ComponentRegistry.hpp>
#include <drift/EntityAllocator.hpp>
#include <drift/components/Sprite.h>
#include <drift/components/Camera.h>

// Resource headers
#include <drift/resources/RendererResource.hpp>
#include <drift/resources/InputResource.hpp>
#include <drift/resources/AudioResource.hpp>
#include <drift/resources/PhysicsResource.hpp>
#include <drift/resources/SpriteResource.hpp>
#include <drift/resources/FontResource.hpp>
#include <drift/resources/ParticleResource.hpp>
#include <drift/resources/TilemapResource.hpp>
#include <drift/resources/UIResource.hpp>
#include <drift/resources/Time.hpp>

// Plugin headers (for DefaultPlugins/MinimalPlugins)
#include <drift/plugins/DefaultPlugins.hpp>
%}

// ---------------------------------------------------------------------------
// Director feature: enable C# to subclass these base classes.
// When C++ calls a virtual method on one of these, SWIG routes the call
// back through the C# override via the P/Invoke director mechanism.
// ---------------------------------------------------------------------------
%feature("director") drift::Plugin;
%feature("director") drift::PluginGroup;
%feature("director") drift::SystemBase;
%feature("director") drift::Script;
%feature("director") drift::EventHandler;

// ---------------------------------------------------------------------------
// Director exception handling: if a C# exception occurs in a director
// callback, catch it on the C++ side and log it instead of crashing.
// ---------------------------------------------------------------------------
%feature("director:except") {
    if ($error != 0) {
        drift::log(drift::LogLevel::Error,
                   "Exception in director callback for %s", "$symname");
    }
}

// ---------------------------------------------------------------------------
// Prevent SWIG from trying to wrap internal/template-only members
// ---------------------------------------------------------------------------

// Resource: hide Res<T> / ResMut<T> templates (guarded by #ifndef SWIG)
%ignore drift::Res;
%ignore drift::ResMut;

// System: hide template machinery (guarded by #ifndef SWIG)
%ignore drift::IsRes;
%ignore drift::IsResMut;
%ignore drift::InnerType;
%ignore drift::DepsCollector;
%ignore drift::SystemTraits;
%ignore drift::ParamBuilder;

// App: hide template methods and private helpers (guarded by #ifndef SWIG)
%ignore drift::App::addResource;
%ignore drift::App::getResource;
%ignore drift::App::addSystem(const char*, Phase, std::function<void(App&)>);
%ignore drift::App::addResourceImpl;
%ignore drift::App::getResourceImpl;
%ignore drift::App::registerSystemFn;
%ignore drift::App::invokeSystem;
%ignore drift::App::invokeSystemHelper;
%ignore drift::App::buildParam;

// EntityBuilder: hide template set<T> (guarded by #ifndef SWIG)
%ignore drift::EntityBuilder::set;

// World: hide template helpers (guarded by #ifndef SWIG)
%ignore drift::World::get;
%ignore drift::World::getMut;
%ignore drift::World::set;
%ignore drift::World::registerComponent(const char*);
%ignore drift::World::componentRegistry;

// App: world()/commands() are exposed via %extend getWorld()/getCommands()
%ignore drift::App::world;
%ignore drift::App::commands;

// C++-only types: EntityAllocator, ComponentRegistry, Query, QueryMut
%ignore drift::EntityAllocator;
%ignore drift::ComponentRegistry;
%ignore drift::Query;
%ignore drift::QueryMut;
%ignore drift::With;
%ignore drift::Without;
%ignore drift::Texture;
%ignore drift::Sound;
%ignore drift::Font;

// Commands: hide C++-only methods
%ignore drift::Commands::Commands;          // constructor (internal)
%ignore drift::Commands::flush;             // internal (called by App)
%ignore drift::Commands::registry;          // C++ only

// EntityCommands: hide operator SWIG can't handle, keep template insert hidden
%ignore drift::EntityCommands::operator EntityId;

// AssetServer: hide loader registration (called by plugins, not users)
%ignore drift::AssetServer::setTextureLoader;
%ignore drift::AssetServer::setSoundLoader;
%ignore drift::AssetServer::setFontLoader;

// Script: hide template getComponent/getComponentMut
%ignore drift::Script::getComponent;
%ignore drift::Script::getComponentMut;

// Ignore SDL_Event parameter - not usable from C#
%ignore drift::EventHandler::processEvent;
%ignore drift::InputResource::processEvent;
%ignore drift::UIResource::processEvent;

// Ignore SDL/GPU internals
%ignore drift::App::sdlWindow;
%ignore drift::App::gpuDevice;
%ignore drift::RendererResource::getGPUDevice;
%ignore drift::RendererResource::getWindow;

// Ignore World internals
%ignore drift::World::flecsWorld;

// Ignore AnimationDef raw pointer members (need special typemap)
%ignore drift::AnimationDef::frames;
%ignore drift::AnimationDef::durations;

// Ignore createTextureFromPixels (raw pixel pointer)
%ignore drift::RendererResource::createTextureFromPixels;

// Ignore addPolygon (raw Vec2 pointer array)
%ignore drift::PhysicsResource::addPolygon;

// Ignore raw pointer output params that need special handling
%ignore drift::PhysicsResource::overlapAABB;
%ignore drift::TilemapResource::getCollisionRects;
%ignore drift::TilemapResource::getObjects;

// Ignore getTextureSize (output params via pointers)
// Provide a wrapped version via %extend instead
%ignore drift::RendererResource::getTextureSize;
%extend drift::RendererResource {
    drift::Vec2 textureSize(drift::TextureHandle texture) const {
        int32_t w = 0, h = 0;
        $self->getTextureSize(texture, &w, &h);
        return drift::Vec2(static_cast<float>(w), static_cast<float>(h));
    }
}

// Ignore slider (float* output param)
%ignore drift::UIResource::slider;

// Ignore QueryIter internal pointer
%ignore drift::QueryIter::_internal;

// Ignore std::vector returns from PhysicsResource
%ignore drift::PhysicsResource::contactBeginEvents;
%ignore drift::PhysicsResource::contactEndEvents;
%ignore drift::PhysicsResource::hitEvents;
%ignore drift::PhysicsResource::sensorBeginEvents;
%ignore drift::PhysicsResource::sensorEndEvents;

// Ignore DRIFT_DEV conditional methods
%ignore drift::UIResource::devToggle;
%ignore drift::UIResource::devRender;
%ignore drift::UIResource::devIsVisible;

// ---------------------------------------------------------------------------
// Ownership transfers: App takes ownership of Plugin*, PluginGroup*,
// SystemBase* passed via these methods. Tell SWIG so the C# GC does not
// try to free the C++ object.
// ---------------------------------------------------------------------------
%newobject drift::App::addPlugin;
%newobject drift::App::addPlugins;
%newobject drift::App::addSystemRaw;
%newobject drift::App::addResourceByName;

// Ownership: %newobject above tells SWIG that addPlugin etc. transfer
// ownership to C++. SWIG's default Dispose will skip the C++ destructor
// when swigCMemOwn is false (which it is after ownership transfer).

// ---------------------------------------------------------------------------
// Script: allow C# to read entity/world/app accessors but not _bind
// ---------------------------------------------------------------------------
%ignore drift::Script::_bind;

// Script convenience: rename position/setPosition to property-friendly names
%rename(GetPosition) drift::Script::position;
%rename(SetPosition) drift::Script::setPosition;

// ---------------------------------------------------------------------------
// Downcast helpers: getResourceByName returns Resource*; provide typed
// accessor helpers so C# code can cast to the concrete resource type.
// ---------------------------------------------------------------------------
%extend drift::App {
    drift::RendererResource* getRendererResource() {
        return static_cast<drift::RendererResource*>(
            $self->getResourceByName("RendererResource"));
    }
    drift::InputResource* getInputResource() {
        return static_cast<drift::InputResource*>(
            $self->getResourceByName("InputResource"));
    }
    drift::AudioResource* getAudioResource() {
        return static_cast<drift::AudioResource*>(
            $self->getResourceByName("AudioResource"));
    }
    drift::PhysicsResource* getPhysicsResource() {
        return static_cast<drift::PhysicsResource*>(
            $self->getResourceByName("PhysicsResource"));
    }
    drift::SpriteResource* getSpriteResource() {
        return static_cast<drift::SpriteResource*>(
            $self->getResourceByName("SpriteResource"));
    }
    drift::FontResource* getFontResource() {
        return static_cast<drift::FontResource*>(
            $self->getResourceByName("FontResource"));
    }
    drift::ParticleResource* getParticleResource() {
        return static_cast<drift::ParticleResource*>(
            $self->getResourceByName("ParticleResource"));
    }
    drift::TilemapResource* getTilemapResource() {
        return static_cast<drift::TilemapResource*>(
            $self->getResourceByName("TilemapResource"));
    }
    drift::UIResource* getUIResource() {
        return static_cast<drift::UIResource*>(
            $self->getResourceByName("UIResource"));
    }
    drift::World* getWorld() {
        return &$self->world();
    }
    drift::Commands* getCommands() {
        return &$self->commands();
    }
    drift::WorldResource* getWorldResource() {
        return static_cast<drift::WorldResource*>(
            $self->getResourceByName("WorldResource"));
    }
    drift::RenderSnapshot* getRenderSnapshot() {
        return static_cast<drift::RenderSnapshot*>(
            $self->getResourceByName("RenderSnapshot"));
    }
    drift::AssetServer* getAssetServer() {
        return static_cast<drift::AssetServer*>(
            $self->getResourceByName("AssetServer"));
    }
    drift::Time* getTime() {
        return static_cast<drift::Time*>(
            $self->getResourceByName("Time"));
    }
}

// ---------------------------------------------------------------------------
// Parse the headers. SWIG respects #ifndef SWIG guards in the headers,
// so template-only code is automatically excluded.
// ---------------------------------------------------------------------------
%include "drift/Resource.hpp"
%include "drift/Plugin.hpp"
%include "drift/System.hpp"
%include "drift/Log.hpp"
%include "drift/App.hpp"
%include "drift/EntityAllocator.hpp"
%include "drift/ComponentRegistry.hpp"
%include "drift/World.hpp"
%include "drift/Entity.hpp"
%include "drift/components/Sprite.h"
%include "drift/components/Camera.h"
%include "drift/Commands.h"
%include "drift/Script.hpp"
%include "drift/RenderSnapshot.h"
%include "drift/WorldResource.h"
%include "drift/AssetServer.h"

// ---------------------------------------------------------------------------
// Resource classes
// ---------------------------------------------------------------------------
%include "drift/resources/RendererResource.hpp"
%include "drift/resources/InputResource.hpp"
%include "drift/resources/AudioResource.hpp"
%include "drift/resources/PhysicsResource.hpp"
%include "drift/resources/SpriteResource.hpp"
%include "drift/resources/FontResource.hpp"
%include "drift/resources/ParticleResource.hpp"
%include "drift/resources/TilemapResource.hpp"
%include "drift/resources/UIResource.hpp"
%include "drift/resources/Time.hpp"

// ---------------------------------------------------------------------------
// Built-in plugin classes (DefaultPlugins, MinimalPlugins)
// These are concrete C++ classes, not director-enabled (no C# subclassing).
// Users can instantiate them from C# and pass to App::addPlugins().
// ---------------------------------------------------------------------------
%include "drift/plugins/DefaultPlugins.hpp"
