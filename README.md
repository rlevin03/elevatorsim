# Elevator Simulator

A multithreaded elevator simulation written in C++20, split across two communicating processes. The controller runs the simulation and accepts commands; the display renders a live ASCII view of the elevator shaft.

## Architecture

```
  elevator_controller              elevator_display
  ┌──────────────────┐             ┌──────────────────┐
  │  movement thread │             │                  │
  │  door thread     │  TCP IPC    │  recv thread     │
  │  broadcast thread├────────────►│  (render loop)   │
  │  client thread   │  port 54321 │                  │
  │  stdin thread    │             └──────────────────┘
  └──────────────────┘
```

The controller serialises elevator state as a newline-delimited text protocol (`FLOOR=X|TARGET=Y|DOOR=Z|QUEUE=a,b\n`) and streams it to the display whenever the state changes.

**Invariants enforced:**
- The elevator never moves while doors are open
- Doors only open after the elevator reaches its target floor
- Every `condition_variable::wait` uses a predicate lambda to guard against spurious wakeups
- Shutdown sets `running = false` then calls `notify_all()` on every condition variable

## Building

Requires CMake 3.20+ and a compiler with C++20 support (GCC 10+, Clang 11+, MSVC 2019+).

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

The first configure will fetch Google Test automatically.

## Running

Start the controller first, then the display in a second terminal.

**Terminal 1 — controller:**
```bash
./elevator_controller
```

**Terminal 2 — display:**
```bash
./elevator_display
```

The display will connect automatically and render the elevator shaft. All commands are entered in the controller terminal:

```
  go <floor>  /  <floor>    request a floor
  hold        /  h          hold door open
  close       /  c          close door now
  quit        /  q          exit
```

## Tests

```bash
cd build
ctest --output-on-failure
```

Or directly for verbose output:

```bash
./elevator_tests
```

| Test | What it checks |
|------|----------------|
| `DefaultConstruction` | Struct initialises to correct defaults |
| `DispatchesImmediatelyWhenIdle` | Idle elevator gets an immediate target, nothing queued |
| `QueuesWhenElevatorIsBusy` | Busy elevator pushes request onto the queue |
| `ReachesSingleRequestedFloor` | Full movement + door cycle lands at the requested floor |
| `ServesMultipleFloorsInOrder` | Queue drains correctly across multiple stops |
| `GracefulShutdownUnblocksThreads` | `shutdown()` unblocks idle threads so joins don't hang |
| `DoorsNeverOpenBetweenFloors` | Polls state during travel, fails if doors open mid-transit |
| `HoldDoorReopensWhenClosing` | HOLD during CLOSING transitions door back to OPEN |

## Project structure

```
elevatorsim/
├── elevator.h          # Elevator struct + public API
├── elevator.cpp        # Simulation logic (movement, door, shutdown)
├── main.cpp            # elevator_controller: TCP server + stdin
├── display.cpp         # elevator_display: TCP client + ASCII renderer
├── CMakeLists.txt      # Build definition (fetches Google Test)
└── tests/
    └── test_elevator.cpp
```
