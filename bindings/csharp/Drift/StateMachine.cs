using System;
using System.Collections.Generic;

namespace Drift
{
    public class StateMachine<TState> where TState : notnull
    {
        private TState _current;
        private readonly Dictionary<TState, Action> _onEnter = new();
        private readonly Dictionary<TState, Action> _onExit = new();

        public TState Current => _current;

        public StateMachine(TState initial)
        {
            _current = initial;
        }

        public void OnEnter(TState state, Action action)
        {
            _onEnter[state] = action;
        }

        public void OnExit(TState state, Action action)
        {
            _onExit[state] = action;
        }

        public void Set(TState newState)
        {
            if (EqualityComparer<TState>.Default.Equals(_current, newState))
                return;

            if (_onExit.TryGetValue(_current, out var exit))
                exit();

            _current = newState;

            if (_onEnter.TryGetValue(_current, out var enter))
                enter();
        }
    }
}
