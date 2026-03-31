#include "elevator.h"

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

// Starts both threads with fast timings and silent output.
// Returns a teardown lambda so each test can shut down cleanly.
static auto make_elevator(Elevator &e)
{
    e.floor_travel_ms = 20;
    e.door_open_ms    = 50;
    e.silent          = true;
    return std::make_pair(
        std::thread(run_movement, std::ref(e)),
        std::thread(run_door,     std::ref(e))
    );
}

// Waits for the elevator to become fully idle (queue drained, doors closed,
// arrived at target). Returns false if the timeout expires first.
static bool wait_idle(Elevator &e, std::chrono::milliseconds timeout = 3s)
{
    std::unique_lock lock{e.mtx};
    return e.cv_idle.wait_for(lock, timeout, [&] {
        return e.requests.empty()
            && e.current_floor == e.target_floor
            && !e.door_open
            && !e.at_target;
    });
}

// ── Unit tests (no threads needed) ───────────────────────────────────────────

TEST(ElevatorState, DefaultConstruction)
{
    Elevator e;
    EXPECT_EQ(e.current_floor, 1);
    EXPECT_EQ(e.target_floor,  1);
    EXPECT_FALSE(e.door_open);
    EXPECT_FALSE(e.at_target);
    EXPECT_TRUE(e.running);
    EXPECT_TRUE(e.requests.empty());
}

TEST(RequestFloor, DispatchesImmediatelyWhenIdle)
{
    Elevator e;
    e.silent = true;
    auto [tm, td] = make_elevator(e);

    request_floor(e, 5);

    {
        std::lock_guard lock{e.mtx};
        EXPECT_EQ(e.target_floor, 5);
        EXPECT_TRUE(e.requests.empty()); // went direct, not queued
    }

    shutdown(e);
    tm.join();
    td.join();
}

TEST(RequestFloor, QueuesWhenElevatorIsBusy)
{
    Elevator e;
    e.silent = true;

    // Simulate elevator already en route to floor 4.
    e.target_floor = 4;

    auto [tm, td] = make_elevator(e);

    request_floor(e, 7);

    {
        std::lock_guard lock{e.mtx};
        EXPECT_EQ(e.requests.size(), 1u);
        EXPECT_EQ(e.requests.front(), 7);
    }

    shutdown(e);
    tm.join();
    td.join();
}

// ── Integration tests ─────────────────────────────────────────────────────────

TEST(Simulation, ReachesSingleRequestedFloor)
{
    Elevator e;
    auto [tm, td] = make_elevator(e);

    request_floor(e, 4);

    ASSERT_TRUE(wait_idle(e)) << "timed out before elevator reached floor 4";

    std::lock_guard lock{e.mtx};
    EXPECT_EQ(e.current_floor, 4);
    EXPECT_FALSE(e.door_open);

    lock.~lock_guard();
    shutdown(e);
    tm.join();
    td.join();
}

TEST(Simulation, ServesMultipleFloorsInOrder)
{
    Elevator e;
    auto [tm, td] = make_elevator(e);

    // Queue two requests. The first is dispatched immediately;
    // the second is picked up by the door thread after the first stop.
    request_floor(e, 3);
    request_floor(e, 6);

    ASSERT_TRUE(wait_idle(e)) << "timed out before elevator served both floors";

    std::lock_guard lock{e.mtx};
    EXPECT_EQ(e.current_floor, 6);
    EXPECT_TRUE(e.requests.empty());

    lock.~lock_guard();
    shutdown(e);
    tm.join();
    td.join();
}

TEST(Simulation, GracefulShutdownUnblocksThreads)
{
    Elevator e;
    auto [tm, td] = make_elevator(e);

    // Shut down immediately with no requests — both threads are blocked on CVs.
    shutdown(e);

    // If shutdown failed to notify, join() would hang and the test would time out.
    tm.join();
    td.join();

    EXPECT_FALSE(e.running);
}

TEST(Invariants, DoorsNeverOpenBetweenFloors)
{
    Elevator e;
    auto [tm, td] = make_elevator(e);

    request_floor(e, 5);

    // Sample the elevator's state periodically during movement and verify that
    // doors are never open when the elevator hasn't reached its target.
    bool violation = false;
    auto deadline = std::chrono::steady_clock::now() + 3s;

    while (std::chrono::steady_clock::now() < deadline)
    {
        {
            std::lock_guard lock{e.mtx};
            if (e.door_open && e.current_floor != e.target_floor)
                violation = true;
            if (e.requests.empty() && e.current_floor == e.target_floor && !e.door_open)
                break;
        }
        std::this_thread::sleep_for(5ms);
    }

    shutdown(e);
    tm.join();
    td.join();

    EXPECT_FALSE(violation) << "doors were open while elevator was between floors";
}
