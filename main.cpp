#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
  #endif
  using socket_t = SOCKET;
  static const socket_t BAD_SOCK = INVALID_SOCKET;
  static void sock_close(socket_t s) { closesocket(s); }
  static void sock_init()    { WSADATA w; WSAStartup(MAKEWORD(2,2), &w); }
  static void sock_cleanup() { WSACleanup(); }
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  using socket_t = int;
  static const socket_t BAD_SOCK = -1;
  static void sock_close(socket_t s) { ::close(s); }
  static void sock_init()    {}
  static void sock_cleanup() {}
#endif

#include "elevator.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

using namespace std::chrono_literals;

static const int PORT = 54321;

static bool send_all(socket_t sock, const std::string& msg) {
    const char* p = msg.c_str();
    int left = (int)msg.size();
    while (left > 0) {
        int n = ::send(sock, p, left, 0);
        if (n <= 0) return false;
        p += n;
        left -= n;
    }
    return true;
}

static void run_broadcast(Elevator& e, socket_t client) {
    // Send initial state immediately, then send on every change.
    std::string last = format_state(e);
    send_all(client, last);

    while (true) {
        {
            std::unique_lock lock{e.mtx};
            e.cv_changed.wait_for(lock, 100ms);
            if (!e.running) break;
        }
        std::string state = format_state(e);
        if (state != last) {
            if (!send_all(client, state)) { shutdown(e); break; }
            last = state;
        }
    }
}

static void run_stdin(Elevator& e, socket_t client) {
    std::cout << "> " << std::flush;
    std::string line;
    while (e.running && std::getline(std::cin, line)) {
        auto first = line.find_first_not_of(" \t\r");
        auto last  = line.find_last_not_of(" \t\r");
        if (first == std::string::npos) { std::cout << "> " << std::flush; continue; }
        line = line.substr(first, last - first + 1);
        for (char& c : line) c = (char)tolower(c);

        if (line == "quit" || line == "q") {
            shutdown(e);
            sock_close(client); // unblocks recv() in run_client
            break;
        } else if (line == "hold" || line == "h") {
            hold_door(e);
        } else if (line == "close" || line == "c") {
            close_door(e);
        } else if (line.rfind("go ", 0) == 0) {
            try {
                int floor = std::stoi(line.substr(3));
                if (floor >= 1 && floor <= 10) request_floor(e, floor);
                else std::cout << "  Floor must be 1-10.\n";
            } catch (...) { std::cout << "  Usage: go <floor>\n"; }
        } else {
            try {
                int floor = std::stoi(line);
                if (floor >= 1 && floor <= 10) request_floor(e, floor);
                else std::cout << "  Floor must be 1-10.\n";
            } catch (...) { std::cout << "  Unknown command.\n"; }
        }
        std::cout << "> " << std::flush;
    }
}

static void run_client(Elevator& e, socket_t client) {
    char buf[256];
    std::string partial;

    while (e.running) {
        int n = recv(client, buf, (int)sizeof(buf) - 1, 0);
        if (n <= 0) { shutdown(e); break; }
        buf[n] = '\0';
        partial += buf;

        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::string cmd = partial.substr(0, pos);
            partial.erase(0, pos + 1);
            if (!cmd.empty() && cmd.back() == '\r') cmd.pop_back();

            if (cmd.rfind("GO ", 0) == 0) {
                try {
                    int floor = std::stoi(cmd.substr(3));
                    if (floor >= 1 && floor <= 10) request_floor(e, floor);
                } catch (...) {}
            } else if (cmd == "HOLD") {
                hold_door(e);
            } else if (cmd == "QUIT") {
                shutdown(e);
                return;
            }
        }
    }
}

int main() {
    sock_init();

    socket_t server = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);
    bind(server, (sockaddr*)&addr, sizeof(addr));
    listen(server, 1);

    elevator_print("+------------------------------------------+");
    elevator_print("|          ELEVATOR CONTROLLER             |");
    elevator_print("+------------------------------------------+");
    elevator_print("  Listening on port " + std::to_string(PORT) + "...");
    elevator_print("  Start elevator_display in another terminal.");

    socket_t client = accept(server, nullptr, nullptr);
    sock_close(server);

    if (client == BAD_SOCK) {
        elevator_print("Accept failed.");
        sock_cleanup();
        return 1;
    }

    elevator_print("\n  Display connected - simulation starting.");
    elevator_print("+------------------------------------------+");
    elevator_print("|               COMMANDS                   |");
    elevator_print("+------------------------------------------+");
    elevator_print("  go <floor>  /  <floor>    request a floor");
    elevator_print("  hold        /  h          hold door open ");
    elevator_print("  close       /  c          close door now ");
    elevator_print("  quit        /  q          exit           ");
    elevator_print("+------------------------------------------+\n");

    Elevator e;

    std::thread t_move(run_movement,   std::ref(e));
    std::thread t_door(run_door,       std::ref(e));
    std::thread t_bcast(run_broadcast, std::ref(e), client);
    std::thread t_client(run_client,   std::ref(e), client);
    std::thread t_stdin(run_stdin,     std::ref(e), client);

    t_stdin.join();
    t_client.join();
    t_bcast.join();
    t_move.join();
    t_door.join();

    sock_close(client);
    sock_cleanup();
    elevator_print("Simulation ended.");
}
