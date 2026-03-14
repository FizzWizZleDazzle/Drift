using System;
using System.Collections.Generic;

namespace Drift
{
    public sealed class Entity
    {
        internal readonly ulong _id;
        internal readonly Dictionary<Type, Component> _components = new();
        internal bool _destroyed;
        private string? _tag;

        internal Entity(ulong ecsId)
        {
            _id = ecsId;
        }

        public ulong Id => _id;
        public bool IsAlive => !_destroyed && EntityRegistry.World.isAlive(_id);

        // Transform properties (read/write ECS Transform2D directly)
        public drift.Vec2 Position
        {
            get
            {
                var ptr = EntityRegistry.World.getComponentMut(_id, EntityRegistry.World.transform2dId());
                if (ptr == IntPtr.Zero) return drift.Vec2.Zero();
                var t = new drift.Transform2D(ptr, false);
                return t.position;
            }
            set
            {
                var ptr = EntityRegistry.World.getComponentMut(_id, EntityRegistry.World.transform2dId());
                if (ptr != IntPtr.Zero)
                {
                    var t = new drift.Transform2D(ptr, false);
                    t.position = value;
                }
            }
        }

        public float Rotation
        {
            get
            {
                var ptr = EntityRegistry.World.getComponentMut(_id, EntityRegistry.World.transform2dId());
                if (ptr == IntPtr.Zero) return 0f;
                var t = new drift.Transform2D(ptr, false);
                return t.rotation;
            }
            set
            {
                var ptr = EntityRegistry.World.getComponentMut(_id, EntityRegistry.World.transform2dId());
                if (ptr != IntPtr.Zero)
                {
                    var t = new drift.Transform2D(ptr, false);
                    t.rotation = value;
                }
            }
        }

        public drift.Vec2 Scale
        {
            get
            {
                var ptr = EntityRegistry.World.getComponentMut(_id, EntityRegistry.World.transform2dId());
                if (ptr == IntPtr.Zero) return drift.Vec2.One();
                var t = new drift.Transform2D(ptr, false);
                return t.scale;
            }
            set
            {
                var ptr = EntityRegistry.World.getComponentMut(_id, EntityRegistry.World.transform2dId());
                if (ptr != IntPtr.Zero)
                {
                    var t = new drift.Transform2D(ptr, false);
                    t.scale = value;
                }
            }
        }

        public string? Name
        {
            get
            {
                var nameId = EntityRegistry.World.nameId();
                if (nameId == 0) return null;
                var ptr = EntityRegistry.World.getComponent(_id, nameId);
                if (ptr == IntPtr.Zero) return null;
                var n = new drift.Name(ptr, false);
                return n.value;
            }
            set
            {
                if (value != null)
                {
                    EntityRegistry.Commands.entity(_id).insert(new drift.Name(value));
                }
            }
        }

        public string? Tag
        {
            get => _tag;
            set
            {
                if (_tag != null) EntityRegistry.RemoveTag(_tag, this);
                _tag = value;
                if (_tag != null) EntityRegistry.AddTag(_tag, this);
            }
        }

        // Component management
        public T Add<T>() where T : Component, new()
        {
            if (_components.ContainsKey(typeof(T)))
                throw new InvalidOperationException($"Entity already has component {typeof(T).Name}");

            var comp = new T();
            comp._entity = this;
            _components[typeof(T)] = comp;
            comp.OnAttach();
            return comp;
        }

        public T Get<T>() where T : Component
        {
            if (_components.TryGetValue(typeof(T), out var comp))
                return (T)comp;
            throw new InvalidOperationException($"Entity does not have component {typeof(T).Name}");
        }

        public T? TryGet<T>() where T : Component
        {
            return _components.TryGetValue(typeof(T), out var comp) ? (T)comp : null;
        }

        public bool Has<T>() where T : Component
        {
            return _components.ContainsKey(typeof(T));
        }

        public void Remove<T>() where T : Component
        {
            if (_components.TryGetValue(typeof(T), out var comp))
            {
                comp.OnDetach();
                comp._entity = null;
                _components.Remove(typeof(T));
            }
        }

        public void Destroy()
        {
            if (_destroyed) return;
            _destroyed = true;
            EntityRegistry.MarkForDestruction(this);
        }

        // Hierarchy
        public void SetParent(Entity parent)
        {
            EntityRegistry.Commands.entity(_id).insertParent(new drift.Parent { entity = parent._id });
        }

        // Fluent builder — adds component, optionally configures it, returns entity
        public Entity With<T>(Action<T>? configure = null) where T : Component, new()
        {
            var comp = Add<T>();
            configure?.Invoke(comp);
            return this;
        }

        // Static shortcut for EntityRegistry.Spawn
        public static Entity Spawn(string? name = null, drift.Vec2? position = null)
            => EntityRegistry.Spawn(name, position);

        // Static queries
        public static List<T> FindAll<T>() where T : Component
        {
            return EntityRegistry.FindAll<T>();
        }

        // Find first component of type (singleton access)
        public static T? Find<T>() where T : Component
        {
            var all = FindAll<T>();
            return all.Count > 0 ? all[0] : null;
        }

        public static Entity? Find(string name)
        {
            return EntityRegistry.FindByName(name);
        }

        public static List<Entity> FindByTag(string tag)
        {
            return EntityRegistry.FindByTag(tag);
        }

        public static void ForEach<T>(Action<T> action) where T : Component
        {
            foreach (var comp in EntityRegistry.FindAll<T>())
                action(comp);
        }
    }
}
