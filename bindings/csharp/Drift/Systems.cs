// =============================================================================
// Drift SDK - Typed System Base Classes (Layer 2)
// =============================================================================
// Hand-written C# SDK that sits on top of SWIG-generated bindings (namespace
// drift) to provide ergonomic, generic system base classes.
//
// System<T1..T8>     : mutable access to resources T1..T8
// QuerySystem<C1,C2> : iterates entities with matching component columns
// =============================================================================

using System;
using System.Collections.Generic;

namespace Drift
{
    // ========================================================================
    // SystemBase adapter: bridges the C# SDK world to the SWIG SystemBase.
    // ========================================================================

    public abstract class SystemBase : drift.SystemBase
    {
        protected abstract Type[] ResourceTypes { get; }
        protected abstract void Invoke(drift.Resource[] resources, float dt);

        public override void execute(drift.App app, float dt)
        {
            var res = new drift.Resource[ResourceTypes.Length];
            for (int i = 0; i < ResourceTypes.Length; i++)
                res[i] = ResourceResolver.Get(app, ResourceTypes[i]);
            Invoke(res, dt);
        }
    }

    // ========================================================================
    // Helper: resolve a Resource subclass from the App by C# type name.
    // ========================================================================

    public static class ResourceResolver
    {
        // C#-side registry for managed resources.
        private static readonly Dictionary<string, drift.Resource> _managed = new();

        // Typed SWIG accessors for engine resources (extensible)
        private static readonly Dictionary<string, Func<drift.App, drift.Resource>> _accessors = new();

        static ResourceResolver()
        {
            RegisterAccessor("RendererResource", a => a.getRendererResource());
            RegisterAccessor("InputResource", a => a.getInputResource());
            RegisterAccessor("AudioResource", a => a.getAudioResource());
            RegisterAccessor("PhysicsResource", a => a.getPhysicsResource());
            RegisterAccessor("SpriteResource", a => a.getSpriteResource());
            RegisterAccessor("FontResource", a => a.getFontResource());
            RegisterAccessor("ParticleResource", a => a.getParticleResource());
            RegisterAccessor("TilemapResource", a => a.getTilemapResource());
            RegisterAccessor("UIResource", a => a.getUIResource());
            RegisterAccessor("WorldResource", a => a.getWorldResource());
            RegisterAccessor("RenderSnapshot", a => a.getRenderSnapshot());
            RegisterAccessor("AssetServer", a => a.getAssetServer());
            RegisterAccessor("Time", a => a.getTime());
        }

        public static void RegisterAccessor(string name, Func<drift.App, drift.Resource> fn)
            => _accessors[name] = fn;

        public static void Register(string name, drift.Resource res)
        {
            _managed[name] = res;
        }

        internal static T Get<T>(drift.App app) where T : drift.Resource
        {
            return (T)Get(app, typeof(T));
        }

        internal static drift.Resource Get(drift.App app, Type type)
        {
            string name = type.Name;

            // Check C#-managed resources first
            if (_managed.TryGetValue(name, out var managed))
                return managed;

            // Use registered SWIG typed accessors
            if (_accessors.TryGetValue(name, out var accessor))
            {
                var result = accessor(app);
                if (result != null) return result;
            }

            // Fallback to name-based lookup
            var raw = app.getResourceByName(name);
            if (raw == null)
            {
                throw new InvalidOperationException(
                    $"Resource '{name}' not found. Was the corresponding " +
                    $"plugin added to the App?");
            }
            return raw;
        }
    }

    // ========================================================================
    // System<T1> -- mutable access to one resource
    // ========================================================================

    public abstract class System<T1> : SystemBase
        where T1 : drift.Resource
    {
        protected override Type[] ResourceTypes => [typeof(T1)];
        protected abstract void Run(T1 res1, float dt);
        protected override void Invoke(drift.Resource[] r, float dt)
            => Run((T1)r[0], dt);
    }

    // ========================================================================
    // System<T1, T2>
    // ========================================================================

    public abstract class System<T1, T2> : SystemBase
        where T1 : drift.Resource
        where T2 : drift.Resource
    {
        protected override Type[] ResourceTypes => [typeof(T1), typeof(T2)];
        protected abstract void Run(T1 res1, T2 res2, float dt);
        protected override void Invoke(drift.Resource[] r, float dt)
            => Run((T1)r[0], (T2)r[1], dt);
    }

    // ========================================================================
    // System<T1, T2, T3>
    // ========================================================================

    public abstract class System<T1, T2, T3> : SystemBase
        where T1 : drift.Resource
        where T2 : drift.Resource
        where T3 : drift.Resource
    {
        protected override Type[] ResourceTypes => [typeof(T1), typeof(T2), typeof(T3)];
        protected abstract void Run(T1 res1, T2 res2, T3 res3, float dt);
        protected override void Invoke(drift.Resource[] r, float dt)
            => Run((T1)r[0], (T2)r[1], (T3)r[2], dt);
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
        protected override Type[] ResourceTypes => [typeof(T1), typeof(T2), typeof(T3), typeof(T4)];
        protected abstract void Run(T1 res1, T2 res2, T3 res3, T4 res4, float dt);
        protected override void Invoke(drift.Resource[] r, float dt)
            => Run((T1)r[0], (T2)r[1], (T3)r[2], (T4)r[3], dt);
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
        protected override Type[] ResourceTypes => [typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5)];
        protected abstract void Run(T1 res1, T2 res2, T3 res3, T4 res4, T5 res5, float dt);
        protected override void Invoke(drift.Resource[] r, float dt)
            => Run((T1)r[0], (T2)r[1], (T3)r[2], (T4)r[3], (T5)r[4], dt);
    }

    // ========================================================================
    // System<T1, T2, T3, T4, T5, T6>
    // ========================================================================

    public abstract class System<T1, T2, T3, T4, T5, T6> : SystemBase
        where T1 : drift.Resource
        where T2 : drift.Resource
        where T3 : drift.Resource
        where T4 : drift.Resource
        where T5 : drift.Resource
        where T6 : drift.Resource
    {
        protected override Type[] ResourceTypes => [typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6)];
        protected abstract void Run(T1 res1, T2 res2, T3 res3, T4 res4, T5 res5, T6 res6, float dt);
        protected override void Invoke(drift.Resource[] r, float dt)
            => Run((T1)r[0], (T2)r[1], (T3)r[2], (T4)r[3], (T5)r[4], (T6)r[5], dt);
    }

    // ========================================================================
    // System<T1, T2, T3, T4, T5, T6, T7>
    // ========================================================================

    public abstract class System<T1, T2, T3, T4, T5, T6, T7> : SystemBase
        where T1 : drift.Resource
        where T2 : drift.Resource
        where T3 : drift.Resource
        where T4 : drift.Resource
        where T5 : drift.Resource
        where T6 : drift.Resource
        where T7 : drift.Resource
    {
        protected override Type[] ResourceTypes => [typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7)];
        protected abstract void Run(T1 res1, T2 res2, T3 res3, T4 res4, T5 res5, T6 res6, T7 res7, float dt);
        protected override void Invoke(drift.Resource[] r, float dt)
            => Run((T1)r[0], (T2)r[1], (T3)r[2], (T4)r[3], (T5)r[4], (T6)r[5], (T7)r[6], dt);
    }

    // ========================================================================
    // System<T1, T2, T3, T4, T5, T6, T7, T8>
    // ========================================================================

    public abstract class System<T1, T2, T3, T4, T5, T6, T7, T8> : SystemBase
        where T1 : drift.Resource
        where T2 : drift.Resource
        where T3 : drift.Resource
        where T4 : drift.Resource
        where T5 : drift.Resource
        where T6 : drift.Resource
        where T7 : drift.Resource
        where T8 : drift.Resource
    {
        protected override Type[] ResourceTypes => [typeof(T1), typeof(T2), typeof(T3), typeof(T4), typeof(T5), typeof(T6), typeof(T7), typeof(T8)];
        protected abstract void Run(T1 res1, T2 res2, T3 res3, T4 res4, T5 res5, T6 res6, T7 res7, T8 res8, float dt);
        protected override void Invoke(drift.Resource[] r, float dt)
            => Run((T1)r[0], (T2)r[1], (T3)r[2], (T4)r[3], (T5)r[4], (T6)r[5], (T7)r[6], (T8)r[7], dt);
    }

    // ========================================================================
    // QuerySystem<C1, C2> -- iterates entities with two component columns
    // ========================================================================

    public abstract class QuerySystem<C1, C2> : SystemBase
    {
        protected override Type[] ResourceTypes => [];
        protected abstract void OnEach(ulong entity, ref C1 comp1, ref C2 comp2, float dt);
        protected virtual void OnBegin(float dt) { }
        protected virtual void OnEnd(float dt) { }

        protected override void Invoke(drift.Resource[] r, float dt)
        {
            OnBegin(dt);

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
                    }
                }
                finally
                {
                    _world.queryFini(iter);
                }
            }

            OnEnd(dt);
        }

        protected void SetWorld(drift.World world)
        {
            _world = world;
        }

        private drift.World? _world;
    }

    // ========================================================================
    // LambdaSystem -- register an inline Action as a system
    // ========================================================================

    public sealed class LambdaSystem : SystemBase
    {
        private readonly Action<drift.App, float> _action;

        public LambdaSystem(Action<drift.App, float> action)
        {
            _action = action ?? throw new ArgumentNullException(nameof(action));
        }

        protected override Type[] ResourceTypes => [];

        protected override void Invoke(drift.Resource[] r, float dt)
        {
            // LambdaSystem bypasses the resource resolution path
        }

        public override void execute(drift.App app, float dt)
        {
            _action(app, dt);
        }
    }
}
