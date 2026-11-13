# drift/ai

Everything `import "drift/ai" { * };` brings in. The [State Machines and AI](../guides/ai.md) guide shows them in use.

## Structs

| Type | Fields and methods |
|------|--------------------|
| `Fsm<S>` | `now`, `previous`, `time`. `tick(dt)`, `go(s)`, `in_state(s)`, `just_entered(s)`. A state machine over an enum `S` of your own |
| `Choice<T>` | `options`, `scores`. `consider(option, score)`, `best()`, `weighted(rng)` |

## Functions

| Function | Returns |
|----------|---------|
| `fsm(start)` | An `Fsm` in state `start` |
| `choice<T>()` | An empty `Choice`; name the option type |
