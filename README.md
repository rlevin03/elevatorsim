# elevatorsim

A multithreaded elevator simulation written in C++20. Two background threads — one for movement, one for doors — run concurrently and are kept in sync using mutexes and condition variables from the C++ standard library. A live command prompt lets you send the elevator to any floor while the simulation is running.

## How it works

```
┌─────────────────────────────────────────────────────┐
│                    main thread                      │
│              interactive command loop               │
└────────────────────┬────────────────────────────────┘
                     │ request_floor()
          ┌──────────▼──────────┐
          │    shared state     │
          │  (Elevator struct)  │
          │  mutex + 3 CVs      │
          └──────┬──────────────┘
                 │                    │
    ┌────────────▼──────┐   ┌─────────▼──────────┐
    │  movement thread  │   │    door thread      │
    │                   │   │                     │
    │  waits: doors     │   │  waits: elevator    │
    │  closed + new     │◄──►  signals arrival    │
    │  target           │   │                     │
    │  moves 1 floor    │   │  opens doors (2s)   │
    │  per tick (400ms) │   │  closes doors       │
    └───────────────────┘   │  loads next request │
                            └─────────────────────┘
```

**Invariants enforced:**
- The elevator never moves while doors are open
- Doors only open after the elevator reaches its target floor
- Every `condition_variable::wait` uses a predicate lambda to guard against spurious wakeups
- Shutdown sets `running = false` then calls `notify_all()` on every condition variable

## Building

**With g++ (MinGW / MSYS2 / Linux / macOS):**
```bash
g++ -std=c++20 -O2 -o elevatorsim main.cpp -lpthread
```

**With CMake:**
```bash
mkdir build && cd build
cmake ..
cmake --build .
```

Requires a compiler with C++20 support (GCC 10+, Clang 11+, MSVC 2019+).

## Running

```
./elevatorsim
```

```
elevatorsim, elevator starts at floor 1

  Commands:
    go <floor>   send the elevator to <floor>  (e.g. 'go 5')
    <floor>      shorthand for 'go <floor>'    (e.g. '5')
    status       show current elevator state
    help         show this message
    quit         shut down and exit

>
```

## Example session

```
> 4
[MOVE] dispatching to floor 4
> 7
[MAIN] queued to floor 7  (queue depth: 1)
> status

  Current floor : 2
  Target floor  : 4
  Doors         : closed
  Queue         : 7

> [MOVE] floor 3
[MOVE] floor 4
[MOVE] arrived at floor 4
[DOOR] opening at floor 4
[DOOR] closing  at floor 4
[DOOR] next target to floor 7
[MOVE] floor 5
> quit
Shutting down...
[MOVE] thread exiting
[DOOR] thread exiting
Goodbye.
```

## Project structure

```
elevatorsim/
├── main.cpp        # entire simulation (~290 lines)
├── CMakeLists.txt  # build definition
└── README.md
```
