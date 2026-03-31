#include "elevator.h"

#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

// Serialises all console output so simulation logs don't tear across the prompt.
static std::mutex g_print_mtx;

void elevator_log(const char *who, const std::string &msg, const Elevator &e)
{
    if (e.silent) return;
    std::lock_guard g{g_print_mtx};
    std::cout << "\r[" << who << "] " << msg << "\n> " << std::flush;
}

void elevator_print(const std::string &msg)
{
    std::lock_guard g{g_print_mtx};
    std::cout << msg << "\n" << std::flush;
}

void run_movement(Elevator &e)
{
    while (true)
    {
        std::unique_lock lock{e.mtx};

        // Wait until there's somewhere to go and the doors are shut.
        e.cv_move.wait(lock, [&] {
            return !e.running || (!e.door_open && e.current_floor != e.target_floor);
        });

        if (!e.running) break;

        e.current_floor += (e.current_floor < e.target_floor) ? 1 : -1;
        elevator_log("MOVE", "floor " + std::to_string(e.current_floor), e);

        if (e.current_floor == e.target_floor)
        {
            e.at_target = true;
            elevator_log("MOVE", "arrived at floor " + std::to_string(e.target_floor), e);
            e.cv_door.notify_one();
        }
        else
        {
            // Release the lock while sleeping between floors.
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(e.floor_travel_ms));
        }
    }

    elevator_log("MOVE", "thread exiting", e);
}

void run_door(Elevator &e)
{
    while (true)
    {
        std::unique_lock lock{e.mtx};

        // Wait until the movement thread signals arrival.
        e.cv_door.wait(lock, [&] {
            return !e.running || e.at_target;
        });

        if (!e.running) break;

        e.door_open = true;
        e.at_target = false;
        elevator_log("DOOR", "opening at floor " + std::to_string(e.current_floor), e);
        lock.unlock();

        std::this_thread::sleep_for(std::chrono::milliseconds(e.door_open_ms));

        lock.lock();
        e.door_open = false;
        elevator_log("DOOR", "closing at floor " + std::to_string(e.current_floor), e);

        // Pick up the next queued request, if any.
        if (!e.requests.empty())
        {
            e.target_floor = e.requests.front();
            e.requests.pop();
            elevator_log("DOOR", "next target to floor " + std::to_string(e.target_floor), e);
            e.cv_move.notify_one();
        }
        else
        {
            elevator_log("DOOR", "queue empty, elevator idle at floor " +
                std::to_string(e.current_floor), e);
            e.cv_idle.notify_all();
        }
    }

    elevator_log("DOOR", "thread exiting", e);
}

void request_floor(Elevator &e, int floor)
{
    std::lock_guard lock{e.mtx};

    const bool idle = (e.current_floor == e.target_floor) && !e.door_open && !e.at_target;

    if (idle)
    {
        e.target_floor = floor;
        elevator_log("MOVE", "dispatching to floor " + std::to_string(floor), e);
        e.cv_move.notify_one();
    }
    else
    {
        e.requests.push(floor);
        elevator_log("MAIN", "queued to floor " + std::to_string(floor) +
            "  (queue depth: " + std::to_string(e.requests.size()) + ")", e);
    }
}

void shutdown(Elevator &e)
{
    {
        std::lock_guard lock{e.mtx};
        e.running = false;
    }
    // Unblock any thread that may be waiting on a CV.
    e.cv_move.notify_all();
    e.cv_door.notify_all();
    e.cv_idle.notify_all();
}
