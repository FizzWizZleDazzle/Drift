// =============================================================================
// Drift SDK - Typed App Extension Methods (Layer 2)
// =============================================================================
// Hand-written C# SDK extension methods for the SWIG-generated drift.App.
// Provides strongly-typed resource accessors and system registration helpers.
//
// SWIG-generated code lives in namespace "drift" (the C++ namespace).
// This hand-written SDK lives in namespace "Drift".
// =============================================================================

using System;

namespace Drift
{
    public static class AppExtensions
    {
        // ====================================================================
        // Typed resource getters
        // ====================================================================

        public static drift.RendererResource? GetRenderer(this drift.App app)
            => app.getRendererResource();

        public static drift.InputResource? GetInput(this drift.App app)
            => app.getInputResource();

        public static drift.AudioResource? GetAudio(this drift.App app)
            => app.getAudioResource();

        public static drift.PhysicsResource? GetPhysics(this drift.App app)
            => app.getPhysicsResource();

        public static drift.SpriteResource? GetSprites(this drift.App app)
            => app.getSpriteResource();

        public static drift.FontResource? GetFonts(this drift.App app)
            => app.getFontResource();

        public static drift.ParticleResource? GetParticles(this drift.App app)
            => app.getParticleResource();

        public static drift.TilemapResource? GetTilemaps(this drift.App app)
            => app.getTilemapResource();

        public static drift.UIResource? GetUI(this drift.App app)
            => app.getUIResource();

        public static drift.Time? GetTime(this drift.App app)
            => app.getTime();

        // ====================================================================
        // World + Commands + AssetServer access
        // ====================================================================

        public static drift.World GetWorld(this drift.App app)
            => app.getWorld();

        public static drift.Commands GetCommands(this drift.App app)
            => app.getCommands();

        public static drift.AssetServer? GetAssets(this drift.App app)
            => app.getAssetServer();

        // ====================================================================
        // Generic resource getter
        // ====================================================================

        public static T? GetResource<T>(this drift.App app) where T : drift.Resource
        {
            return app.getResourceByName(typeof(T).Name) as T;
        }

        public static T RequireResource<T>(this drift.App app) where T : drift.Resource
        {
            var res = app.getResourceByName(typeof(T).Name) as T;
            if (res == null)
            {
                throw new InvalidOperationException(
                    $"Resource '{typeof(T).Name}' is required but was not " +
                    $"registered. Ensure the corresponding plugin is added " +
                    $"to the App before this system runs.");
            }
            return res;
        }

        // ====================================================================
        // System registration
        // ====================================================================

        public static drift.App AddSystem<T>(this drift.App app, drift.Phase phase)
            where T : Drift.SystemBase, new()
        {
            var system = new T();
            string name = typeof(T).Name;
            app.addSystemRaw(name, phase, system);
            return app;
        }

        public static drift.App AddSystem(this drift.App app, Drift.SystemBase system, drift.Phase phase)
        {
            if (system == null) throw new ArgumentNullException(nameof(system));
            string name = system.GetType().Name;
            app.addSystemRaw(name, phase, system);
            return app;
        }

        public static drift.App AddSystem(
            this drift.App app,
            string name,
            drift.Phase phase,
            Action<drift.App, float> action)
        {
            if (action == null) throw new ArgumentNullException(nameof(action));
            var system = new LambdaSystem(action);
            app.addSystemRaw(name, phase, system);
            return app;
        }

        // ====================================================================
        // Single-call resource registration
        // ====================================================================

        public static drift.App AddResource<T>(this drift.App app, T resource) where T : drift.Resource
        {
            string name = resource.name();
            app.addResourceByName(name, resource);
            ResourceResolver.Register(name, resource);
            return app;
        }

        // ====================================================================
        // Phase shortcuts
        // ====================================================================

        public static drift.App Startup<T>(this drift.App app) where T : Drift.SystemBase, new()
            => app.AddSystem<T>(drift.Phase.Startup);

        public static drift.App Update<T>(this drift.App app) where T : Drift.SystemBase, new()
            => app.AddSystem<T>(drift.Phase.Update);

        public static drift.App Render<T>(this drift.App app) where T : Drift.SystemBase, new()
            => app.AddSystem<T>(drift.Phase.Render);

        public static drift.App PreUpdate<T>(this drift.App app) where T : Drift.SystemBase, new()
            => app.AddSystem<T>(drift.Phase.PreUpdate);

        public static drift.App PostUpdate<T>(this drift.App app) where T : Drift.SystemBase, new()
            => app.AddSystem<T>(drift.Phase.PostUpdate);

        public static drift.App Extract<T>(this drift.App app) where T : Drift.SystemBase, new()
            => app.AddSystem<T>(drift.Phase.Extract);

        // ====================================================================
        // Plugin-like builder helpers
        // ====================================================================

        public static drift.App WithConfig(this drift.App app, drift.Config config)
        {
            app.setConfig(config);
            return app;
        }

        public static drift.App WithPlugin(this drift.App app, drift.Plugin plugin)
        {
            app.addPlugin(plugin);
            return app;
        }

        public static drift.App WithPlugins(this drift.App app, drift.PluginGroup group)
        {
            app.addPlugins(group);
            return app;
        }
    }
}
