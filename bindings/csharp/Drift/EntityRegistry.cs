using System;
using System.Collections.Generic;

namespace Drift
{
    public static class EntityRegistry
    {
        internal static drift.App? App;
        internal static drift.World World => App!.getWorld();
        internal static drift.Commands Commands => App!.getCommands();

        private static readonly Dictionary<ulong, Entity> _entities = new();
        private static readonly List<Entity> _pendingDestroy = new();
        private static readonly Dictionary<string, List<Entity>> _tags = new();

        public static void Initialize(drift.App app)
        {
            App = app;
        }

        public static Entity Spawn(string? name = null, drift.Vec2? position = null)
        {
            var cmd = Commands;
            var pos = position ?? drift.Vec2.Zero();
            var ec = cmd.spawn()
                .insert(new drift.Transform2D { position = pos });

            if (name != null)
                ec.insert(new drift.Name(name));

            var entity = new Entity(ec.id());
            _entities[entity._id] = entity;
            return entity;
        }

        public static Entity? GetEntity(ulong id)
        {
            return _entities.TryGetValue(id, out var e) ? e : null;
        }

        // Get or create a lightweight wrapper for entities not spawned via C#
        public static Entity GetOrWrap(ulong id)
        {
            if (_entities.TryGetValue(id, out var existing))
                return existing;
            var wrapper = new Entity(id);
            _entities[id] = wrapper;
            return wrapper;
        }

        internal static void AddTag(string tag, Entity entity)
        {
            if (!_tags.TryGetValue(tag, out var list))
            {
                list = new List<Entity>();
                _tags[tag] = list;
            }
            list.Add(entity);
        }

        internal static void RemoveTag(string tag, Entity entity)
        {
            if (_tags.TryGetValue(tag, out var list))
                list.Remove(entity);
        }

        internal static void MarkForDestruction(Entity entity)
        {
            _pendingDestroy.Add(entity);
        }

        public static void Update(float dt)
        {
            // 1. Poll collision events
            DispatchCollisions();

            // 2. Call OnUpdate on all components
            foreach (var entity in _entities.Values)
            {
                if (entity._destroyed) continue;
                // Copy to array to allow modification during iteration
                var components = new List<Component>(entity._components.Values);
                foreach (var comp in components)
                    comp.OnUpdate(dt);
            }

            // 3. Process timer callbacks (handled by Timer component's OnUpdate)

            // 4. Cleanup destroyed entities
            foreach (var entity in _pendingDestroy)
            {
                if (entity.Tag != null)
                    RemoveTag(entity.Tag, entity);

                var components = new List<Component>(entity._components.Values);
                foreach (var comp in components)
                {
                    comp.OnDetach();
                    comp._entity = null;
                }
                entity._components.Clear();

                Commands.despawn(entity._id);
                _entities.Remove(entity._id);
            }
            _pendingDestroy.Clear();
        }

        private static void DispatchCollisions()
        {
            var bridge = App?.getCollisionBridge();
            if (bridge == null) return;

            // Collision start
            for (int i = 0; i < bridge.collisionStartCount(); i++)
            {
                ulong a = bridge.collisionStartA(i);
                ulong b = bridge.collisionStartB(i);
                if (_entities.TryGetValue(a, out var ea))
                {
                    var other = GetOrWrap(b);
                    foreach (var c in ea._components.Values) c.OnCollisionEnter(other);
                }
                if (_entities.TryGetValue(b, out var eb))
                {
                    var other = GetOrWrap(a);
                    foreach (var c in eb._components.Values) c.OnCollisionEnter(other);
                }
            }

            // Collision end
            for (int i = 0; i < bridge.collisionEndCount(); i++)
            {
                ulong a = bridge.collisionEndA(i);
                ulong b = bridge.collisionEndB(i);
                if (_entities.TryGetValue(a, out var ea))
                {
                    var other = GetOrWrap(b);
                    foreach (var c in ea._components.Values) c.OnCollisionExit(other);
                }
                if (_entities.TryGetValue(b, out var eb))
                {
                    var other = GetOrWrap(a);
                    foreach (var c in eb._components.Values) c.OnCollisionExit(other);
                }
            }

            // Sensor start
            for (int i = 0; i < bridge.sensorStartCount(); i++)
            {
                ulong a = bridge.sensorStartA(i);
                ulong b = bridge.sensorStartB(i);
                if (_entities.TryGetValue(a, out var ea))
                {
                    var other = GetOrWrap(b);
                    foreach (var c in ea._components.Values) c.OnSensorEnter(other);
                }
                if (_entities.TryGetValue(b, out var eb))
                {
                    var other = GetOrWrap(a);
                    foreach (var c in eb._components.Values) c.OnSensorEnter(other);
                }
            }

            // Sensor end
            for (int i = 0; i < bridge.sensorEndCount(); i++)
            {
                ulong a = bridge.sensorEndA(i);
                ulong b = bridge.sensorEndB(i);
                if (_entities.TryGetValue(a, out var ea))
                {
                    var other = GetOrWrap(b);
                    foreach (var c in ea._components.Values) c.OnSensorExit(other);
                }
                if (_entities.TryGetValue(b, out var eb))
                {
                    var other = GetOrWrap(a);
                    foreach (var c in eb._components.Values) c.OnSensorExit(other);
                }
            }
        }

        public static List<T> FindAll<T>() where T : Component
        {
            var result = new List<T>();
            foreach (var entity in _entities.Values)
            {
                if (entity._destroyed) continue;
                if (entity._components.TryGetValue(typeof(T), out var comp))
                    result.Add((T)comp);
            }
            return result;
        }

        public static Entity? FindByName(string name)
        {
            foreach (var entity in _entities.Values)
            {
                if (entity._destroyed) continue;
                if (entity.Name == name) return entity;
            }
            return null;
        }

        public static List<Entity> FindByTag(string tag)
        {
            if (_tags.TryGetValue(tag, out var list))
                return new List<Entity>(list);
            return new List<Entity>();
        }

        public static void Clear()
        {
            _entities.Clear();
            _pendingDestroy.Clear();
            _tags.Clear();
        }
    }
}
