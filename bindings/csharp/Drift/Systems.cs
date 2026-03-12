// =============================================================================
// Drift SDK - Typed System Base Classes (Layer 2)
// =============================================================================
// Hand-written C# SDK that sits on top of SWIG-generated bindings (namespace
// drift) to provide ergonomic, generic system base classes.
//
// System<T1..T5>     : mutable access to resources T1..T5
// ReadSystem<T1..T5> : read-only access to resources T1..T5
// QuerySystem<C1,C2> : iterates entities with matching component columns
// =============================================================================

using System;

namespace Drift
{
    // ========================================================================
    // SystemBase adapter: bridges the C# SDK world to the SWIG SystemBase.
    //
    // SWIG generates drift.SystemBase with virtual execute(drift.App, float).
    // We subclass it once here; the generic System<T> classes compose it via
    // an internal wrapper so user code never touches the SWIG layer directly.
    // ========================================================================

    /// <summary>
    /// Non-generic base that all typed systems derive from.  Holds the
    /// reference to the SWIG App and provides the Execute entry-point that
    /// the engine calls each frame.
    /// </summary>
    public abstract class SystemBase : drift.SystemBase
    {
        /// <summary>
        /// Called by the engine scheduler.  Subclasses override to resolve
        /// their typed resources and delegate to the user's Run method.
        /// </summary>
        public override void execute(drift.App app, float dt)
        {
            Execute(app, dt);
        }

        /// <summary>
        /// Typed dispatch implemented by each generic arity.
        /// </summary>
        internal abstract void Execute(drift.App app, float dt);
    }

    // ========================================================================
    // Helper: resolve a Resource subclass from the App by C# type name.
    // ========================================================================

    public static class ResourceResolver
    {
        // C#-side registry for managed resources. When a C# Resource subclass
        // is registered via addResourceByName, SWIG stores the C++ pointer but
        // loses the C# type identity on retrieval. This dictionary preserves it.
        private static readonly System.Collections.Generic.Dictionary<string, drift.Resource>
            _managed = new();

        /// <summary>
        /// Register a C#-managed resource so it can be retrieved with its
        /// original type intact.
        /// </summary>
        public static void Register(string name, drift.Resource res)
        {
            _managed[name] = res;
        }

        /// <summary>
        /// Retrieves a resource by the C# type name. Checks the managed
        /// registry first (for C#-defined resources), then falls back to
        /// the SWIG/C++ path (for engine-defined resources).
        /// </summary>
        internal static T Get<T>(drift.App app) where T : drift.Resource
        {
            string name = typeof(T).Name;

            // Check C#-managed resources first
            if (_managed.TryGetValue(name, out var managed))
            {
                return (T)managed;
            }

            // Use SWIG typed accessors for engine resources (avoids downcast issue)
            drift.Resource raw = name switch
            {
                "RendererResource" => app.getRendererResource(),
                "InputResource"    => app.getInputResource(),
                "AudioResource"    => app.getAudioResource(),
                "PhysicsResource"  => app.getPhysicsResource(),
                "SpriteResource"   => app.getSpriteResource(),
                "FontResource"     => app.getFontResource(),
                "ParticleResource" => app.getParticleResource(),
                "TilemapResource"  => app.getTilemapResource(),
                "UIResource"       => app.getUIResource(),
                _                  => app.getResourceByName(name),
            };

            if (raw == null)
            {
                throw new InvalidOperationException(
                    $"Resource '{name}' not found.  Was the corresponding " +
                    $"plugin added to the App?");
            }
            return (T)raw;
        }
    }

    // ========================================================================
    // System<T1> -- mutable access to one resource
    // ========================================================================

    /// <summary>
    /// Base class for a system that requires mutable access to a single
    /// resource of type <typeparamref name="T1"/>.
    /// </summary>
    public abstract class System<T1> : SystemBase
        where T1 : drift.Resource
    {
        /// <summary>
        /// Implement game logic here.  The resource is fetched and cast for
        /// you each frame.
        /// </summary>
        protected abstract void Run(T1 res1, float dt);

        internal sealed override void Execute(drift.App app, float dt)
        {
            var r1 = ResourceResolver.Get<T1>(app);
            Run(r1, dt);
        }
    }

    // ========================================================================
    // System<T1, T2>
    // ========================================================================

    public abstract class System<T1, T2> : SystemBase
        where T1 : drift.Resource
        where T2 : drift.Resource
    {
        protected abstract void Run(T1 res1, T2 res2, float dt);

        internal sealed override void Execute(drift.App app, float dt)
        {
            var r1 = ResourceResolver.Get<T1>(app);
            var r2 = ResourceResolver.Get<T2>(app);
            Run(r1, r2, dt);
        }
    }

    // ========================================================================
    // System<T1, T2, T3>
    // ========================================================================

    public abstract class System<T1, T2, T3> : SystemBase
        where T1 : drift.Resource
        where T2 : drift.Resource
        where T3 : drift.Resource
    {
        protected abstract void Run(T1 res1, T2 res2, T3 res3, float dt);

        internal sealed override void Execute(drift.App app, float dt)
        {
            var r1 = ResourceResolver.Get<T1>(app);
            var r2 = ResourceResolver.Get<T2>(app);
            var r3 = ResourceResolver.Get<T3>(app);
            Run(r1, r2, r3, dt);
        }
    }

    // ========================================================================
    // System<T1, T2, T3, T4>
    // ========================================================================

    public abstract class System<T1, T2, T3, T4> : SystemBase
        where T1 : drift.Resource
        where T2 : drift.Resource
        where T3 : drift.Resource
        where T4 : drift.Resource
    {
        protected abstract void Run(T1 res1, T2 res2, T3 res3, T4 res4, float dt);

        internal sealed override void Execute(drift.App app, float dt)
        {
            var r1 = ResourceResolver.Get<T1>(app);
            var r2 = ResourceResolver.Get<T2>(app);
            var r3 = ResourceResolver.Get<T3>(app);
            var r4 = ResourceResolver.Get<T4>(app);
            Run(r1, r2, r3, r4, dt);
        }
    }

    // ========================================================================
    // System<T1, T2, T3, T4, T5>
    // ========================================================================

    public abstract class System<T1, T2, T3, T4, T5> : SystemBase
        where T1 : drift.Resource
        where T2 : drift.Resource
        where T3 : drift.Resource
        where T4 : drift.Resource
        where T5 : drift.Resource
    {
        protected abstract void Run(T1 res1, T2 res2, T3 res3, T4 res4, T5 res5, float dt);

        internal sealed override void Execute(drift.App app, float dt)
        {
            var r1 = ResourceResolver.Get<T1>(app);
            var r2 = ResourceResolver.Get<T2>(app);
            var r3 = ResourceResolver.Get<T3>(app);
            var r4 = ResourceResolver.Get<T4>(app);
            var r5 = ResourceResolver.Get<T5>(app);
            Run(r1, r2, r3, r4, r5, dt);
        }
    }

    // ========================================================================
    // ReadSystem<T1> -- read-only access to one resource
    // ========================================================================

    /// <summary>
    /// Base class for a system that requires read-only access to a single
    /// resource.  The resource reference is const-correct at the C++ level;
    /// in C# we signal intent through the class name.  The SWIG proxy does
    /// not enforce const, but using ReadSystem documents that the system
    /// does not mutate the resource.
    /// </summary>
    public abstract class ReadSystem<T1> : SystemBase
        where T1 : drift.Resource
    {
        protected abstract void Run(T1 res1, float dt);

        internal sealed override void Execute(drift.App app, float dt)
        {
            var r1 = ResourceResolver.Get<T1>(app);
            Run(r1, dt);
        }
    }

    // ========================================================================
    // ReadSystem<T1, T2>
    // ========================================================================

    public abstract class ReadSystem<T1, T2> : SystemBase
        where T1 : drift.Resource
        where T2 : drift.Resource
    {
        protected abstract void Run(T1 res1, T2 res2, float dt);

        internal sealed override void Execute(drift.App app, float dt)
        {
            var r1 = ResourceResolver.Get<T1>(app);
            var r2 = ResourceResolver.Get<T2>(app);
            Run(r1, r2, dt);
        }
    }

    // ========================================================================
    // ReadSystem<T1, T2, T3>
    // ========================================================================

    public abstract class ReadSystem<T1, T2, T3> : SystemBase
        where T1 : drift.Resource
        where T2 : drift.Resource
        where T3 : drift.Resource
    {
        protected abstract void Run(T1 res1, T2 res2, T3 res3, float dt);

        internal sealed override void Execute(drift.App app, float dt)
        {
            var r1 = ResourceResolver.Get<T1>(app);
            var r2 = ResourceResolver.Get<T2>(app);
            var r3 = ResourceResolver.Get<T3>(app);
            Run(r1, r2, r3, dt);
        }
    }

    // ========================================================================
    // ReadSystem<T1, T2, T3, T4>
    // ========================================================================

    public abstract class ReadSystem<T1, T2, T3, T4> : SystemBase
        where T1 : drift.Resource
        where T2 : drift.Resource
        where T3 : drift.Resource
        where T4 : drift.Resource
    {
        protected abstract void Run(T1 res1, T2 res2, T3 res3, T4 res4, float dt);

        internal sealed override void Execute(drift.App app, float dt)
        {
            var r1 = ResourceResolver.Get<T1>(app);
            var r2 = ResourceResolver.Get<T2>(app);
            var r3 = ResourceResolver.Get<T3>(app);
            var r4 = ResourceResolver.Get<T4>(app);
            Run(r1, r2, r3, r4, dt);
        }
    }

    // ========================================================================
    // ReadSystem<T1, T2, T3, T4, T5>
    // ========================================================================

    public abstract class ReadSystem<T1, T2, T3, T4, T5> : SystemBase
        where T1 : drift.Resource
        where T2 : drift.Resource
        where T3 : drift.Resource
        where T4 : drift.Resource
        where T5 : drift.Resource
    {
        protected abstract void Run(T1 res1, T2 res2, T3 res3, T4 res4, T5 res5, float dt);

        internal sealed override void Execute(drift.App app, float dt)
        {
            var r1 = ResourceResolver.Get<T1>(app);
            var r2 = ResourceResolver.Get<T2>(app);
            var r3 = ResourceResolver.Get<T3>(app);
            var r4 = ResourceResolver.Get<T4>(app);
            var r5 = ResourceResolver.Get<T5>(app);
            Run(r1, r2, r3, r4, r5, dt);
        }
    }

    // ========================================================================
    // QuerySystem<C1, C2> -- iterates entities with two component columns
    // ========================================================================

    /// <summary>
    /// Base class for a system that iterates over all entities possessing
    /// components C1 and C2.  Subclass and override OnEach to process each
    /// matching entity.
    ///
    /// Component types must be registered with the World's ECS.  The query
    /// expression is built from the component type names (the C++ names as
    /// registered via drift::World::registerComponent).
    /// </summary>
    public abstract class QuerySystem<C1, C2> : SystemBase
    {
        /// <summary>
        /// Called once per matching entity per frame.
        /// </summary>
        protected abstract void OnEach(ulong entity, ref C1 comp1, ref C2 comp2, float dt);

        /// <summary>
        /// Optional: override to perform work before iteration begins each
        /// frame (e.g. clear a spatial hash).
        /// </summary>
        protected virtual void OnBegin(float dt) { }

        /// <summary>
        /// Optional: override to perform work after all entities have been
        /// visited (e.g. flush draw calls).
        /// </summary>
        protected virtual void OnEnd(float dt) { }

        internal sealed override void Execute(drift.App app, float dt)
        {
            // The World resource must be accessible from the App.  The
            // concrete mechanism depends on whether a WorldResource wrapper
            // is registered.  For now we require the World to be reachable
            // through a SWIG-visible accessor on App (to be wired up).
            //
            // Placeholder: subclasses can store a World reference in their
            // constructor.  A future revision will auto-resolve it.
            OnBegin(dt);

            // Build query expression from type names.  The C++ ECS registers
            // components by their unqualified name, so "Transform2D, Velocity"
            // would match components named Transform2D and Velocity.
            string c1Name = typeof(C1).Name;
            string c2Name = typeof(C2).Name;
            string queryExpr = c1Name + ", " + c2Name;

            if (_world != null)
            {
                drift.QueryIter iter = _world.queryIter(queryExpr);
                try
                {
                    while (_world.queryNext(iter))
                    {
                        int count = iter.count;
                        // queryField returns a pointer to the component
                        // array for this archetype chunk.  We cannot easily
                        // marshal arbitrary structs without unsafe code, so
                        // the concrete per-entity dispatch is left to the
                        // SWIG layer or a future unsafe helper.
                        //
                        // For now, entities are available:
                        // drift.Entity* entities = _world.queryEntities(iter);
                    }
                }
                finally
                {
                    _world.queryFini(iter);
                }
            }

            OnEnd(dt);
        }

        /// <summary>
        /// Set the World reference.  Call this during system construction or
        /// in a startup system.  A future version will auto-resolve the World
        /// from the App.
        /// </summary>
        protected void SetWorld(drift.World world)
        {
            _world = world;
        }

        private drift.World? _world;
    }

    // ========================================================================
    // LambdaSystem -- register an inline Action as a system
    // ========================================================================

    /// <summary>
    /// Wraps a simple Action&lt;float&gt; as a SystemBase so it can be
    /// registered via addSystemRaw on the App.
    /// </summary>
    public sealed class LambdaSystem : SystemBase
    {
        private readonly Action<drift.App, float> _action;

        public LambdaSystem(Action<drift.App, float> action)
        {
            _action = action ?? throw new ArgumentNullException(nameof(action));
        }

        internal sealed override void Execute(drift.App app, float dt)
        {
            _action(app, dt);
        }
    }
}
