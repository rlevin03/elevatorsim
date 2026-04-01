#include "elevator.h"

#include <chrono>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

using ms = std::chrono::milliseconds;

static std::mutex g_print_mtx;

void elevator_log(const char* who, const std::string& msg, const Elevator& e) {
    if (e.silent) return;
    std::lock_guard g{g_print_mtx};
    std::cout << "[" << who << "] " << msg << "\n" << std::flush;
}

void elevator_print(const std::string& msg) {
    std::lock_guard g{g_print_mtx};
    std::cout << msg << "\n" << std::flush;
}

void run_movement(Elevator& e) {
    while (true) {
        std::unique_lock lock{e.mtx};

        e.cv_move.wait(lock, [&] {
            return !e.running ||
                   (e.door_state == DoorState::CLOSED && e.current_floor != e.target_floor);
        });

        if (!e.running) break;

        e.current_floor += (e.current_floor < e.target_floor) ? 1 : -1;
        elevator_log("MOVE", "floor " + std::to_string(e.current_floor), e);
        e.cv_changed.notify_all();

        if (e.current_floor == e.target_floor) {
            e.at_target = true;
            elevator_log("MOVE", "arrived at floor " + std::to_string(e.target_floor), e);
            e.cv_door.notify_one();
        } else {
            lock.unlock(); // release while sleeping so other threads remain responsive
            std::this_thread::sleep_for(ms(e.floor_travel_ms));
        }
    }
    elevator_log("MOVE", "thread exiting", e);
}

void run_door(Elevator& e) {
    while (true) {
        {
            std::unique_lock lock{e.mtx};
            e.cv_door.wait(lock, [&] { return !e.running || e.at_target; });
            if (!e.running) break;
            e.at_target = false;
        }

        // OPENING — not interruptible
        {
            std::unique_lock lock{e.mtx};
            e.door_state = DoorState::OPENING;
            elevator_log("DOOR", "opening", e);
            e.cv_changed.notify_all();
            lock.unlock();
            std::this_thread::sleep_for(ms(e.door_open_ms));
        }

        // OPEN/CLOSING loop — repeats if the close is interrupted by a hold
        while (true) {
            {
                std::unique_lock lock{e.mtx};
                e.door_state = DoorState::OPEN;
                elevator_log("DOOR", "open", e);
                e.cv_changed.notify_all();

                bool woken = e.cv_hold.wait_for(lock, ms(e.door_stay_ms),
                    [&] { return !e.running || e.hold_requested || e.close_requested; });
                if (!e.running) return;
                if (woken && e.hold_requested) {
                    e.hold_requested = false;
                    elevator_log("DOOR", "hold - dwell timer reset", e);
                    continue;
                }
                if (woken && e.close_requested) {
                    e.close_requested = false;
                    elevator_log("DOOR", "closing early", e);
                    // fall through to CLOSING
                }
            }

            {
                std::unique_lock lock{e.mtx};
                e.door_state = DoorState::CLOSING;
                elevator_log("DOOR", "closing", e);
                e.cv_changed.notify_all();

                bool held = e.cv_hold.wait_for(lock, ms(e.door_close_ms),
                    [&] { return !e.running || e.hold_requested; });
                if (!e.running) return;
                if (held && e.hold_requested) {
                    e.hold_requested = false;
                    elevator_log("DOOR", "hold - reopening", e);
                    continue;
                }
                break;
            }
        }

        {
            std::unique_lock lock{e.mtx};
            e.door_state = DoorState::CLOSED;
            elevator_log("DOOR", "closed", e);

            if (!e.requests.empty()) {
                e.target_floor = e.requests.front();
                e.requests.pop();
                elevator_log("DOOR", "next target floor " + std::to_string(e.target_floor), e);
                e.cv_move.notify_one();
            } else {
                elevator_log("DOOR", "queue empty, idle at floor " +
                    std::to_string(e.current_floor), e);
                e.cv_idle.notify_all();
            }
            e.cv_changed.notify_all();
        }
    }
    elevator_log("DOOR", "thread exiting", e);
}

void request_floor(Elevator& e, int floor) {
    std::lock_guard lock{e.mtx};

    const bool idle = (e.current_floor == e.target_floor)
                   && e.door_state == DoorState::CLOSED
                   && !e.at_target;

    if (idle) {
        e.target_floor = floor;
        elevator_log("MOVE", "dispatching to floor " + std::to_string(floor), e);
        e.cv_move.notify_one();
    } else {
        e.requests.push(floor);
        elevator_log("MAIN", "queued floor " + std::to_string(floor) +
            "  (depth: " + std::to_string(e.requests.size()) + ")", e);
    }
    e.cv_changed.notify_all();
}

void hold_door(Elevator& e) {
    std::lock_guard lock{e.mtx};
    if (e.door_state == DoorState::OPEN || e.door_state == DoorState::CLOSING) {
        e.hold_requested = true;
        e.cv_hold.notify_one();
        elevator_log("DOOR", "hold requested", e);
    }
}

void close_door(Elevator& e) {
    std::lock_guard lock{e.mtx};
    if (e.door_state == DoorState::OPEN) {
        e.close_requested = true;
        e.cv_hold.notify_one();
        elevator_log("DOOR", "close requested", e);
    }
}

void shutdown(Elevator& e) {
    {
        std::lock_guard lock{e.mtx};
        e.running = false;
    }
    e.cv_move.notify_all();
    e.cv_door.notify_all();
    e.cv_hold.notify_all();
    e.cv_idle.notify_all();
    e.cv_changed.notify_all();
}

std::string format_state(Elevator& e) {
    std::lock_guard lock{e.mtx};
    std::ostringstream oss;
    oss << "FLOOR=" << e.current_floor
        << "|TARGET=" << e.target_floor
        << "|DOOR=" << door_state_cstr(e.door_state)
        << "|QUEUE=";
    auto q = e.requests;
    bool first = true;
    while (!q.empty()) {
        if (!first) oss << ",";
        oss << q.front();
        q.pop();
        first = false;
    }
    oss << "\n";
    return oss.str();
}
