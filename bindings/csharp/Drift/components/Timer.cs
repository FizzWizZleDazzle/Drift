using System;

namespace Drift
{
    public class Timer : Component
    {
        private float _remaining;
        private Action? _callback;
        private bool _active;

        public void Set(float seconds, Action callback)
        {
            _remaining = seconds;
            _callback = callback;
            _active = true;
        }

        public void Cancel()
        {
            _active = false;
            _callback = null;
        }

        public bool IsActive => _active;
        public float Remaining => _remaining;

        protected internal override void OnUpdate(float dt)
        {
            if (!_active) return;
            _remaining -= dt;
            if (_remaining <= 0f)
            {
                _active = false;
                _callback?.Invoke();
                _callback = null;
            }
        }
    }
}
