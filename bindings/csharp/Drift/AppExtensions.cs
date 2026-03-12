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
    /// <summary>
    /// Extension methods for <see cref="drift.App"/> that provide typed
    /// resource accessors and system registration helpers.
    /// </summary>
    public static class AppExtensions
    {
        // ====================================================================
        // Typed resource getters
        // ====================================================================
        // Each method fetches the resource by its registered name (which
        // matches the C++ class name returned by Resource::name()) and casts
        // to the concrete SWIG proxy type.

        /// <summary>
        /// Returns the RendererResource, or null if not registered.
        /// </summary>
        public static drift.RendererResource? GetRenderer(this drift.App app)
        {
            return app.getResourceByName("RendererResource") as drift.RendererResource;
        }

        /// <summary>
        /// Returns the InputResource, or null if not registered.
        /// </summary>
        public static drift.InputResource? GetInput(this drift.App app)
        {
            return app.getResourceByName("InputResource") as drift.InputResource;
        }

        /// <summary>
        /// Returns the AudioResource, or null if not registered.
        /// </summary>
        public static drift.AudioResource? GetAudio(this drift.App app)
        {
            return app.getResourceByName("AudioResource") as drift.AudioResource;
        }

        /// <summary>
        /// Returns the PhysicsResource, or null if not registered.
        /// </summary>
        public static drift.PhysicsResource? GetPhysics(this drift.App app)
        {
            return app.getResourceByName("PhysicsResource") as drift.PhysicsResource;
        }

        /// <summary>
        /// Returns the SpriteResource, or null if not registered.
        /// </summary>
        public static drift.SpriteResource? GetSprites(this drift.App app)
        {
            return app.getResourceByName("SpriteResource") as drift.SpriteResource;
        }

        /// <summary>
        /// Returns the FontResource, or null if not registered.
        /// </summary>
        public static drift.FontResource? GetFonts(this drift.App app)
        {
            return app.getResourceByName("FontResource") as drift.FontResource;
        }

        /// <summary>
        /// Returns the ParticleResource, or null if not registered.
        /// </summary>
        public static drift.ParticleResource? GetParticles(this drift.App app)
        {
            return app.getResourceByName("ParticleResource") as drift.ParticleResource;
        }

        /// <summary>
        /// Returns the TilemapResource, or null if not registered.
        /// </summary>
        public static drift.TilemapResource? GetTilemaps(this drift.App app)
        {
            return app.getResourceByName("TilemapResource") as drift.TilemapResource;
        }

        /// <summary>
        /// Returns the UIResource, or null if not registered.
        /// </summary>
        public static drift.UIResource? GetUI(this drift.App app)
        {
            return app.getResourceByName("UIResource") as drift.UIResource;
        }

        // ====================================================================
        // Generic resource getter
        // ====================================================================

        /// <summary>
        /// Retrieves a resource by its C# type name.  The type name must
        /// match the string returned by the C++ Resource::name() virtual.
        /// Returns null if not found.
        /// </summary>
        public static T? GetResource<T>(this drift.App app) where T : drift.Resource
        {
            return app.getResourceByName(typeof(T).Name) as T;
        }

        /// <summary>
        /// Retrieves a resource by its C# type name.  Throws
        /// <see cref="InvalidOperationException"/> if not found.
        /// </summary>
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

        /// <summary>
        /// Creates a new instance of system type <typeparamref name="T"/>
        /// and registers it with the App at the given phase.
        ///
        /// <typeparamref name="T"/> must be a concrete subclass of one of
        /// the generic System or ReadSystem base classes from Drift.Systems.
        ///
        /// Example:
        /// <code>
        /// app.AddSystem&lt;MyMovementSystem&gt;(drift.Phase.Update);
        /// </code>
        /// </summary>
        public static drift.App AddSystem<T>(this drift.App app, drift.Phase phase)
            where T : Drift.SystemBase, new()
        {
            var system = new T();
            string name = typeof(T).Name;
            app.addSystemRaw(name, phase, system);
            return app;
        }

        /// <summary>
        /// Registers an existing system instance with the App.
        /// Use this overload when you need to pass constructor arguments.
        ///
        /// Example:
        /// <code>
        /// var sys = new MySystem(someConfig);
        /// app.AddSystem(sys, drift.Phase.Update);
        /// </code>
        /// </summary>
        public static drift.App AddSystem(this drift.App app, Drift.SystemBase system, drift.Phase phase)
        {
            if (system == null) throw new ArgumentNullException(nameof(system));
            string name = system.GetType().Name;
            app.addSystemRaw(name, phase, system);
            return app;
        }

        /// <summary>
        /// Registers a lambda / delegate as a system.  Convenient for quick
        /// prototyping.
        ///
        /// Example:
        /// <code>
        /// app.AddSystem("DebugOverlay", drift.Phase.Render, (app, dt) =>
        /// {
        ///     var renderer = app.GetRenderer();
        ///     // ... draw debug info ...
        /// });
        /// </code>
        /// </summary>
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

        // ====================================================================
        // Plugin-like builder helpers
        // ====================================================================

        /// <summary>
        /// Fluent configuration helper.  Sets the Config on the App and
        /// returns it for chaining.
        /// </summary>
        public static drift.App WithConfig(this drift.App app, drift.Config config)
        {
            app.setConfig(config);
            return app;
        }

        /// <summary>
        /// Adds a plugin and returns the App for chaining.
        /// </summary>
        public static drift.App WithPlugin(this drift.App app, drift.Plugin plugin)
        {
            app.addPlugin(plugin);
            return app;
        }

        /// <summary>
        /// Adds a plugin group and returns the App for chaining.
        /// </summary>
        public static drift.App WithPlugins(this drift.App app, drift.PluginGroup group)
        {
            app.addPlugins(group);
            return app;
        }
    }
}
