#include "elevator.h"

#include <iostream>
#include <sstream>
#include <string>
#include <thread>

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
            if (!q.empty()) oss << ", ";
        }
    }

    elevator_print(oss.str());
}

static void print_help()
{
    elevator_print(
        "\n  Commands:"
        "\n    go <floor>   send the elevator to <floor>  (e.g. 'go 5')"
        "\n    <floor>      shorthand for go              (e.g. '5')"
        "\n    status       show current elevator state"
        "\n    help         show this message"
        "\n    quit         shut down and exit\n");
}

int main()
{
    Elevator e;

    std::thread t_move(run_movement, std::ref(e));
    std::thread t_door(run_door, std::ref(e));

    elevator_print("elevatorsim — elevator starts at floor 1");
    print_help();

    {
        std::lock_guard g{e.mtx}; // borrow mtx just to flush the prompt cleanly
        (void)g;
        std::cout << "> " << std::flush;
    }

    std::string line;
    while (std::getline(std::cin, line))
    {
        const auto first = line.find_first_not_of(" \t\r");
        const auto last  = line.find_last_not_of(" \t\r");
        if (first == std::string::npos)
        {
            std::cout << "> " << std::flush;
            continue;
        }
        line = line.substr(first, last - first + 1);

        std::istringstream iss{line};
        std::string cmd;
        iss >> cmd;

        if (cmd == "quit" || cmd == "exit" || cmd == "q")
        {
            elevator_print("Shutting down...");
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
                elevator_print("  Usage: go <floor>  (floor must be between 1 and 20)");
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
                    elevator_print("  Floor must be between 1 and 20.");
                else
                    request_floor(e, floor);
            }
            catch (...)
            {
                elevator_print("  Unknown command '" + cmd + "'. Type 'help' for usage.");
            }
        }

        std::cout << "> " << std::flush;
    }

    t_move.join();
    t_door.join();

    elevator_print("Goodbye.");
}
