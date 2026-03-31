#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>

using namespace std::chrono_literals;

// Serialises all console output so simulation logs don't tear across the prompt.
static std::mutex g_print_mtx;

static void log(const char *who, const std::string &msg)
{
    std::lock_guard g{g_print_mtx};
    std::cout << "\r[" << who << "] " << msg << "\n> " << std::flush;
}

static void print(const std::string &msg)
{
    std::lock_guard g{g_print_mtx};
    std::cout << msg << "\n"
              << std::flush;
}

struct Elevator
{
    int current_floor = 1;
    int target_floor = 1;
    bool door_open = false;
    bool at_target = false; // set by movement thread on arrival, cleared by door thread
    bool running = true;

    std::queue<int> requests;
    std::mutex mtx;
    std::condition_variable cv_move; // wakes movement thread
    std::condition_variable cv_door; // wakes door thread
    std::condition_variable cv_idle; // wakes main when queue is drained
};

void run_movement(Elevator &e)
{
    while (true)
    {
        std::unique_lock lock{e.mtx};

        // Wait until there's somewhere to go and the doors are shut.
        e.cv_move.wait(lock, [&]
                       { return !e.running || (!e.door_open && e.current_floor != e.target_floor); });

        if (!e.running)
            break;

        e.current_floor += (e.current_floor < e.target_floor) ? 1 : -1;
        log("MOVE", "floor " + std::to_string(e.current_floor));

        if (e.current_floor == e.target_floor)
        {
            e.at_target = true;
            log("MOVE", "arrived at floor " + std::to_string(e.target_floor));
            e.cv_door.notify_one();
        }
        else
        {
            // Release the lock while sleeping between floors.
            lock.unlock();
            std::this_thread::sleep_for(400ms);
        }
    }

    log("MOVE", "thread exiting");
}

void run_door(Elevator &e)
{
    while (true)
    {
        std::unique_lock lock{e.mtx};

        // Wait until the movement thread signals arrival.
        e.cv_door.wait(lock, [&]
                       { return !e.running || e.at_target; });

        if (!e.running)
            break;

        e.door_open = true;
        e.at_target = false;
        log("DOOR", "opening at floor " + std::to_string(e.current_floor));
        lock.unlock();

        std::this_thread::sleep_for(2s); // doors stay open

        lock.lock();
        e.door_open = false;
        log("DOOR", "closing at floor " + std::to_string(e.current_floor));

        // Pick up the next queued request, if any.
        if (!e.requests.empty())
        {
            e.target_floor = e.requests.front();
            e.requests.pop();
            log("DOOR", "next target to floor " + std::to_string(e.target_floor));
            e.cv_move.notify_one();
        }
        else
        {
            log("DOOR", "queue empty, elevator idle at floor " + std::to_string(e.current_floor));
            e.cv_idle.notify_all();
        }
    }

    log("DOOR", "thread exiting");
}

// If idle, send immediately. Otherwise push to the queue for the door thread to pick up.
static void request_floor(Elevator &e, int floor)
{
    std::lock_guard lock{e.mtx};

    const bool idle = (e.current_floor == e.target_floor) && !e.door_open && !e.at_target;

    if (idle)
    {
        e.target_floor = floor;
        log("MOVE", "dispatching to floor " + std::to_string(floor));
        e.cv_move.notify_one();
    }
    else
    {
        e.requests.push(floor);
        log("MAIN", "queued to floor " + std::to_string(floor) +
                        "  (queue depth: " + std::to_string(e.requests.size()) + ")");
    }
}

static void print_status(Elevator &e)
{
    std::lock_guard lock{e.mtx};

    std::ostringstream oss;
    oss << "\n  Current floor : " << e.current_floor
        << "\n  Target floor  : " << e.target_floor
        << "\n  Doors         : " << (e.door_open ? "OPEN" : "closed")
        << "\n  Queue         : ";

    if (e.requests.empty())
    {
        oss << "(empty)";
    }
    else
    {
        // std::queue has no iterators, so copy it to print without consuming.
        auto q = e.requests;
        while (!q.empty())
        {
            oss << q.front();
            q.pop();
            if (!q.empty())
                oss << ", ";
        }
    }

    print(oss.str());
}

static void print_help()
{
    print(
        "\n  Commands:"
        "\n    go <floor>   send the elevator to <floor>  (e.g. 'go 5')"
        "\n    <floor>      shorthand for go              (e.g. '5')"
        "\n    status       show current elevator state"
        "\n    help         show this message"
        "\n    quit         shut down and exit\n");
}

static void shutdown(Elevator &e)
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

int main()
{
    Elevator e;

    std::thread t_move(run_movement, std::ref(e));
    std::thread t_door(run_door, std::ref(e));

    print("elevatorsim, elevator starts at floor 1");
    print_help();

    {
        std::lock_guard g{g_print_mtx};
        std::cout << "> " << std::flush;
    }

    std::string line;
    while (std::getline(std::cin, line))
    {
        const auto first = line.find_first_not_of(" \t\r");
        const auto last = line.find_last_not_of(" \t\r");
        if (first == std::string::npos)
        {
            std::lock_guard g{g_print_mtx};
            std::cout << "> " << std::flush;
            continue;
        }
        line = line.substr(first, last - first + 1);

        std::istringstream iss{line};
        std::string cmd;
        iss >> cmd;

        if (cmd == "quit" || cmd == "exit" || cmd == "q")
        {
            print("Shutting down...");
            shutdown(e);
            break;
        }
        else if (cmd == "status")
        {
            print_status(e);
        }
        else if (cmd == "help" || cmd == "h" || cmd == "?")
        {
            print_help();
        }
        else if (cmd == "go")
        {
            int floor = 0;
            if (!(iss >> floor) || floor < 1 || floor > 20)
                print("Usage: go <floor>  (floor must be between 1 and 20)");
            else
                request_floor(e, floor);
        }
        else
        {
            // Allow bare floor numbers as shorthand for 'go'.
            try
            {
                const int floor = std::stoi(cmd);
                if (floor < 1 || floor > 20)
                    print("Floor must be between 1 and 20.");
                else
                    request_floor(e, floor);
            }
            catch (...)
            {
                print("Unknown command '" + cmd + "'. Type 'help' for usage.");
            }
        }

        std::lock_guard g{g_print_mtx};
        std::cout << "> " << std::flush;
    }

    t_move.join();
    t_door.join();

    print("Goodbye.");
}
