using System;

namespace Drift
{
    public abstract class Component
    {
        internal Entity? _entity;

        public Entity Entity => _entity ?? throw new InvalidOperationException("Component not attached to entity");

        // Service accessors (available from any component)
        protected static drift.InputResource Input => EntityRegistry.App!.getInputResource();
        protected static drift.AudioResource Audio => EntityRegistry.App!.getAudioResource();
        protected static drift.AssetServer Assets => EntityRegistry.App!.getAssetServer();
        protected static drift.RendererResource Renderer => EntityRegistry.App!.getRendererResource();
        protected static drift.UIResource UI => EntityRegistry.App!.getUIResource();
        protected static drift.PhysicsResource Physics => EntityRegistry.App!.getPhysicsResource();
        protected static drift.World World => EntityRegistry.App!.getWorld();
        protected static drift.Commands Commands => EntityRegistry.App!.getCommands();

        // Lifecycle hooks
        protected internal virtual void OnAttach() { }
        protected internal virtual void OnDetach() { }
        protected internal virtual void OnUpdate(float dt) { }

        // Collision callbacks
        protected internal virtual void OnCollisionEnter(Entity other) { }
        protected internal virtual void OnCollisionExit(Entity other) { }
        protected internal virtual void OnSensorEnter(Entity other) { }
        protected internal virtual void OnSensorExit(Entity other) { }
    }
}
