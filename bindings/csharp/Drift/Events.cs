using System;
using System.Collections.Generic;

namespace Drift
{
    public static class Events
    {
        private static readonly Dictionary<Type, List<Delegate>> _handlers = new();

        public static void On<T>(Action<T> handler)
        {
            var type = typeof(T);
            if (!_handlers.TryGetValue(type, out var list))
            {
                list = new List<Delegate>();
                _handlers[type] = list;
            }
            list.Add(handler);
        }

        public static void Off<T>(Action<T> handler)
        {
            if (_handlers.TryGetValue(typeof(T), out var list))
                list.Remove(handler);
        }

        public static void Emit<T>(T evt)
        {
            if (_handlers.TryGetValue(typeof(T), out var list))
            {
                foreach (var handler in list)
                    ((Action<T>)handler)(evt);
            }
        }

        public static void Clear<T>()
        {
            _handlers.Remove(typeof(T));
        }

        public static void ClearAll()
        {
            _handlers.Clear();
        }
    }
}
