#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

enum class DoorState { CLOSED, OPENING, OPEN, CLOSING };

inline const char* door_state_cstr(DoorState s) {
    switch (s) {
        case DoorState::CLOSED:  return "CLOSED";
        case DoorState::OPENING: return "OPENING";
        case DoorState::OPEN:    return "OPEN";
        case DoorState::CLOSING: return "CLOSING";
    }
    return "";
}

struct Elevator {
    int       current_floor = 1;
    int       target_floor  = 1;
    DoorState door_state    = DoorState::CLOSED;

    bool at_target       = false; // set by movement thread on arrival, cleared by door thread
    bool hold_requested  = false;
    bool close_requested = false;
    bool running         = true;

    // Timing in ms — override in tests for short durations.
    int floor_travel_ms = 2000;
    int door_open_ms    = 1000;
    int door_stay_ms    = 5000;
    int door_close_ms   = 3000;

    bool silent = false;

    std::queue<int>         requests;
    std::mutex              mtx;
    std::condition_variable cv_move;    // wakes movement thread
    std::condition_variable cv_door;    // wakes door thread on arrival
    std::condition_variable cv_hold;    // wakes door thread on hold/close signal
    std::condition_variable cv_idle;    // wakes waiters when queue is drained
    std::condition_variable cv_changed; // notified on any state change
};

void run_movement(Elevator& e);
void run_door(Elevator& e);

void request_floor(Elevator& e, int floor);
void hold_door(Elevator& e);
void close_door(Elevator& e);
void shutdown(Elevator& e);

// Takes its own lock — do not call while holding e.mtx.
std::string format_state(Elevator& e);

void elevator_log(const char* who, const std::string& msg, const Elevator& e);
void elevator_print(const std::string& msg);
