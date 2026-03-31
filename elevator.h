#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

struct Elevator
{
    int  current_floor   = 1;
    int  target_floor    = 1;
    bool door_open       = false;
    bool at_target       = false; // set by movement thread on arrival, cleared by door thread
    bool running         = true;

    // Configurable per-instance so tests can use short durations.
    int door_open_ms    = 2000;
    int floor_travel_ms =  400;

    // Suppress console output (set to true in tests).
    bool silent = false;

    std::queue<int>         requests;
    std::mutex              mtx;
    std::condition_variable cv_move; // wakes movement thread
    std::condition_variable cv_door; // wakes door thread
    std::condition_variable cv_idle; // wakes main when queue is drained
};

void run_movement(Elevator &e);
void run_door(Elevator &e);

// If idle, dispatch immediately. Otherwise push to queue for door thread to pick up.
void request_floor(Elevator &e, int floor);

// Set running=false and unblock all waiting threads. Caller must join threads after.
void shutdown(Elevator &e);

// Thread-safe output helpers used by the simulation and the UI.
void elevator_log(const char *who, const std::string &msg, const Elevator &e);
void elevator_print(const std::string &msg);
