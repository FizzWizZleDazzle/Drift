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
#include <drift/Commands.hpp>
#include <drift/RenderSnapshot.hpp>
#include <drift/WorldResource.h>
#include <drift/AssetServer.hpp>
#include <drift/ComponentRegistry.hpp>
#include <drift/EntityAllocator.hpp>
#include <drift/components/Sprite.hpp>
#include <drift/components/Camera.hpp>
#include <drift/components/Physics.hpp>
#include <drift/components/Name.hpp>
#include <drift/components/Hierarchy.hpp>
#include <drift/components/GlobalTransform.hpp>
#include <drift/components/ParticleEmitter.hpp>
#include <drift/components/SpriteAnimator.hpp>
#include <drift/components/CameraController.hpp>
#include <drift/components/TrailRenderer.hpp>
#include <drift/particles/EmitterConfig.hpp>
#include <drift/CollisionBridge.hpp>

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
%ignore drift::RendererResource::setPrePassCallback;
%ignore drift::RendererResource::setInPassCallback;

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
// Component types: ignore std::vector fields, provide helpers
// ---------------------------------------------------------------------------

// SpriteAnimator: ignore vector<AnimationClip> clips
%ignore drift::SpriteAnimator::clips;
%ignore drift::AnimationClip;
%extend drift::SpriteAnimator {
    void addClip(const drift::Rect* frames, int frameCount, float frameDuration, bool loop) {
        drift::AnimationClip clip;
        clip.frameDuration = frameDuration;
        clip.looping = loop;
        for (int i = 0; i < frameCount; i++) {
            clip.frames.push_back(frames[i]);
        }
        $self->clips.push_back(clip);
    }
    int clipCount() const { return static_cast<int>($self->clips.size()); }
    void clearClips() { $self->clips.clear(); }
}

// EmitterConfig: ignore vector<BurstEntry> bursts
%ignore drift::EmitterConfig::bursts;
%extend drift::EmitterConfig {
    void addBurst(float time, int count, int cycles, float interval) {
        drift::BurstEntry burst;
        burst.time = time;
        burst.count = count;
        burst.cycles = cycles;
        burst.interval = interval;
        $self->bursts.push_back(burst);
    }
    int burstCount() const { return static_cast<int>($self->bursts.size()); }
    void clearBursts() { $self->bursts.clear(); }
}

// Children: ignore fixed-size array, provide indexed access
%ignore drift::Children::ids;
%extend drift::Children {
    uint64_t getId(int i) const {
        return (i >= 0 && i < $self->count) ? $self->ids[i] : 0;
    }
    void setId(int i, uint64_t id) {
        if (i >= 0 && i < drift::MaxChildren) $self->ids[i] = id;
    }
}

// CollisionBridge: hide internal Entry struct and mutator methods
%ignore drift::CollisionBridge::Entry;
%ignore drift::CollisionBridge::clear;
%ignore drift::CollisionBridge::addCollisionStart;
%ignore drift::CollisionBridge::addCollisionEnd;
%ignore drift::CollisionBridge::addSensorStart;
%ignore drift::CollisionBridge::addSensorEnd;

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
    drift::CollisionBridge* getCollisionBridge() {
        return static_cast<drift::CollisionBridge*>(
            $self->getResourceByName("CollisionBridge"));
    }
}

// ---------------------------------------------------------------------------
// EntityCommands: typed inserts for all component types
// ---------------------------------------------------------------------------
%extend drift::EntityCommands {
    drift::EntityCommands& insertRigidBody(const drift::RigidBody2D& v) {
        $self->insert(v);
        return *$self;
    }
    drift::EntityCommands& insertBoxCollider(const drift::BoxCollider2D& v) {
        $self->insert(v);
        return *$self;
    }
    drift::EntityCommands& insertCircleCollider(const drift::CircleCollider2D& v) {
        $self->insert(v);
        return *$self;
    }
    drift::EntityCommands& insertVelocity(const drift::Velocity2D& v) {
        $self->insert(v);
        return *$self;
    }
    drift::EntityCommands& insertParent(const drift::Parent& v) {
        $self->insert(v);
        return *$self;
    }
    drift::EntityCommands& insertChildren(const drift::Children& v) {
        $self->insert(v);
        return *$self;
    }
    drift::EntityCommands& insertCameraFollow(const drift::CameraFollow& v) {
        $self->insert(v);
        return *$self;
    }
    drift::EntityCommands& insertCameraShake(const drift::CameraShake& v) {
        $self->insert(v);
        return *$self;
    }
    drift::EntityCommands& insertTrailRenderer(const drift::TrailRenderer& v) {
        $self->insert(v);
        return *$self;
    }
    drift::EntityCommands& insertParticleEmitter(const drift::ParticleEmitter& v) {
        $self->insert(v);
        return *$self;
    }
    drift::EntityCommands& insertSpriteAnimator(const drift::SpriteAnimator& v) {
        $self->insert(v);
        return *$self;
    }
    drift::EntityCommands& insertGlobalTransform(const drift::GlobalTransform2D& v) {
        $self->insert(v);
        return *$self;
    }
}

// ---------------------------------------------------------------------------
// World: component ID lookups for C# direct component access
// ---------------------------------------------------------------------------
%extend drift::World {
    uint64_t rigidBody2dId()     { return $self->lookupComponent("RigidBody2D"); }
    uint64_t boxCollider2dId()   { return $self->lookupComponent("BoxCollider2D"); }
    uint64_t circleCollider2dId(){ return $self->lookupComponent("CircleCollider2D"); }
    uint64_t velocity2dId()      { return $self->lookupComponent("Velocity2D"); }
    uint64_t parentComponentId() { return $self->lookupComponent("Parent"); }
    uint64_t childrenId()        { return $self->lookupComponent("Children"); }
    uint64_t cameraFollowId()    { return $self->lookupComponent("CameraFollow"); }
    uint64_t cameraShakeId()     { return $self->lookupComponent("CameraShake"); }
    uint64_t trailRendererId()   { return $self->lookupComponent("TrailRenderer"); }
    uint64_t particleEmitterId() { return $self->lookupComponent("ParticleEmitter"); }
    uint64_t spriteAnimatorId()  { return $self->lookupComponent("SpriteAnimator"); }
    uint64_t globalTransform2dId() { return $self->lookupComponent("GlobalTransform2D"); }
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
// Component types (must come BEFORE Commands.hpp so SWIG resolves
// types used in EntityCommands::insert() overloads correctly)
%include "drift/components/Sprite.hpp"
%include "drift/components/Camera.hpp"
%include "drift/components/Name.hpp"
%include "drift/components/Hierarchy.hpp"
%include "drift/components/GlobalTransform.hpp"
%include "drift/components/Physics.hpp"
%include "drift/particles/EmitterConfig.hpp"
%include "drift/components/ParticleEmitter.hpp"
%include "drift/components/SpriteAnimator.hpp"
%include "drift/components/CameraController.hpp"
%include "drift/components/TrailRenderer.hpp"
%include "drift/CollisionBridge.hpp"

%include "drift/Commands.hpp"
%include "drift/Script.hpp"
%include "drift/RenderSnapshot.hpp"
%include "drift/WorldResource.h"
%include "drift/AssetServer.hpp"

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
