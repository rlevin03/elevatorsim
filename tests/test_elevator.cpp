#include "elevator.h"

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

using namespace std::chrono_literals;

static auto make_elevator(Elevator& e) {
    e.floor_travel_ms = 20;
    e.door_open_ms    = 10;
    e.door_stay_ms    = 30;
    e.door_close_ms   = 20;
    e.silent          = true;
    return std::make_pair(
        std::thread(run_movement, std::ref(e)),
        std::thread(run_door,     std::ref(e))
    );
}

static bool wait_idle(Elevator& e, std::chrono::milliseconds timeout = 3s) {
    std::unique_lock lock{e.mtx};
    return e.cv_idle.wait_for(lock, timeout, [&] {
        return e.requests.empty()
            && e.current_floor == e.target_floor
            && e.door_state == DoorState::CLOSED
            && !e.at_target;
    });
}

TEST(ElevatorState, DefaultConstruction) {
    Elevator e;
    EXPECT_EQ(e.current_floor, 1);
    EXPECT_EQ(e.target_floor,  1);
    EXPECT_EQ(e.door_state, DoorState::CLOSED);
    EXPECT_FALSE(e.at_target);
    EXPECT_FALSE(e.hold_requested);
    EXPECT_TRUE(e.running);
    EXPECT_TRUE(e.requests.empty());
}

TEST(RequestFloor, DispatchesImmediatelyWhenIdle) {
    Elevator e;
    e.silent = true;
    auto [tm, td] = make_elevator(e);

    request_floor(e, 5);

    {
        std::lock_guard lock{e.mtx};
        EXPECT_EQ(e.target_floor, 5);
        EXPECT_TRUE(e.requests.empty());
    }

    shutdown(e);
    tm.join();
    td.join();
}

TEST(RequestFloor, QueuesWhenElevatorIsBusy) {
    Elevator e;
    e.silent = true;
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

TEST(Simulation, ReachesSingleRequestedFloor) {
    Elevator e;
    auto [tm, td] = make_elevator(e);

    request_floor(e, 4);

    ASSERT_TRUE(wait_idle(e)) << "timed out before elevator reached floor 4";

    {
        std::lock_guard lock{e.mtx};
        EXPECT_EQ(e.current_floor, 4);
        EXPECT_EQ(e.door_state, DoorState::CLOSED);
    }

    shutdown(e);
    tm.join();
    td.join();
}

TEST(Simulation, ServesMultipleFloorsInOrder) {
    Elevator e;
    auto [tm, td] = make_elevator(e);

    request_floor(e, 3);
    request_floor(e, 6);

    ASSERT_TRUE(wait_idle(e)) << "timed out before elevator served both floors";

    {
        std::lock_guard lock{e.mtx};
        EXPECT_EQ(e.current_floor, 6);
        EXPECT_TRUE(e.requests.empty());
    }

    shutdown(e);
    tm.join();
    td.join();
}

TEST(Simulation, GracefulShutdownUnblocksThreads) {
    Elevator e;
    auto [tm, td] = make_elevator(e);

    shutdown(e);

    tm.join();
    td.join();

    EXPECT_FALSE(e.running);
}

TEST(Invariants, DoorsNeverOpenBetweenFloors) {
    Elevator e;
    auto [tm, td] = make_elevator(e);

    request_floor(e, 5);

    bool violation = false;
    auto deadline  = std::chrono::steady_clock::now() + 3s;

    while (std::chrono::steady_clock::now() < deadline) {
        {
            std::lock_guard lock{e.mtx};
            if (e.door_state != DoorState::CLOSED && e.current_floor != e.target_floor)
                violation = true;
            if (e.requests.empty() && e.current_floor == e.target_floor
                    && e.door_state == DoorState::CLOSED)
                break;
        }
        std::this_thread::sleep_for(5ms);
    }

    shutdown(e);
    tm.join();
    td.join();

    EXPECT_FALSE(violation) << "doors were not CLOSED while elevator was between floors";
}

TEST(DoorBehavior, CloseDoorSkipsDwell) {
    Elevator e;
    e.floor_travel_ms = 20;
    e.door_open_ms    = 10;
    e.door_stay_ms    = 5000; // long dwell — close_door should cut it short
    e.door_close_ms   = 20;
    e.silent          = true;
    auto tm = std::thread(run_movement, std::ref(e));
    auto td = std::thread(run_door,     std::ref(e));

    request_floor(e, 2);

    {
        std::unique_lock lock{e.mtx};
        bool opened = e.cv_changed.wait_for(lock, 3s,
            [&] { return e.door_state == DoorState::OPEN; });
        ASSERT_TRUE(opened) << "door never entered OPEN state";
    }

    close_door(e);

    {
        std::unique_lock lock{e.mtx};
        bool closed = e.cv_changed.wait_for(lock, 2s,
            [&] { return e.door_state == DoorState::CLOSED; });
        EXPECT_TRUE(closed) << "door did not close after CLOSE command";
    }

    shutdown(e);
    tm.join();
    td.join();
}

TEST(DoorBehavior, HoldDoorReopensWhenClosing) {
    Elevator e;
    e.floor_travel_ms = 20;
    e.door_open_ms    = 10;
    e.door_stay_ms    = 30;
    e.door_close_ms   = 500;
    e.silent          = true;
    auto tm = std::thread(run_movement, std::ref(e));
    auto td = std::thread(run_door,     std::ref(e));

    request_floor(e, 3);

    {
        std::unique_lock lock{e.mtx};
        bool reached = e.cv_changed.wait_for(lock, 3s,
            [&] { return e.door_state == DoorState::CLOSING; });
        ASSERT_TRUE(reached) << "door never entered CLOSING state";
    }

    hold_door(e);

    {
        std::unique_lock lock{e.mtx};
        bool reopened = e.cv_changed.wait_for(lock, 2s,
            [&] { return e.door_state == DoorState::OPEN; });
        EXPECT_TRUE(reopened) << "door did not reopen after HOLD during CLOSING";
    }

    shutdown(e);
    tm.join();
    td.join();
}
