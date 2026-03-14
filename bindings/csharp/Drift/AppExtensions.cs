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

        public static drift.CollisionBridge? GetCollisionBridge(this drift.App app)
            => app.getCollisionBridge();

        // ====================================================================
        // Lambda system registration
        // ====================================================================

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
        // Resource registration
        // ====================================================================

        public static drift.App AddResource(this drift.App app, drift.Resource resource)
        {
            string name = resource.name();
            app.addResourceByName(name, resource);
            return app;
        }

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

    // Internal: LambdaSystem for inline Action registration
    internal sealed class LambdaSystem : drift.SystemBase
    {
        private readonly Action<drift.App, float> _action;

        public LambdaSystem(Action<drift.App, float> action)
        {
            _action = action ?? throw new ArgumentNullException(nameof(action));
        }

        public override void execute(drift.App app, float dt)
        {
            _action(app, dt);
        }
    }
}
