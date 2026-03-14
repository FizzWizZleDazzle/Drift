using System;

namespace Drift
{
    public abstract class DriftApp
    {
        public abstract string Title { get; }
        public abstract int Width { get; }
        public abstract int Height { get; }
        public virtual bool Resizable => false;

        // Service accessors (same as Component)
        protected static drift.AssetServer Assets => EntityRegistry.App!.getAssetServer();
        protected static drift.AudioResource Audio => EntityRegistry.App!.getAudioResource();
        protected static drift.InputResource Input => EntityRegistry.App!.getInputResource();
        protected static drift.RendererResource Renderer => EntityRegistry.App!.getRendererResource();

        protected abstract void OnStartup();

        public static int Run<T>(string[] args) where T : DriftApp, new()
        {
            var game = new T();
            var app = new drift.App();

            var config = new drift.Config();
            config.title = game.Title;
            config.width = game.Width;
            config.height = game.Height;
            config.resizable = game.Resizable;
            app.setConfig(config);

            app.addPlugins(new drift.DefaultPlugins());
            EntityRegistry.Initialize(app);

            app.AddSystem("startup", drift.Phase.Startup, (a, dt) => game.OnStartup());
            app.AddSystem("entity_registry_update", drift.Phase.Update, (a, dt) => EntityRegistry.Update(dt));

            return app.run();
        }
    }
}
