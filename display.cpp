#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #ifdef _MSC_VER
    #pragma comment(lib, "ws2_32.lib")
  #endif
  #include <windows.h>
  using socket_t = SOCKET;
  static const socket_t BAD_SOCK = INVALID_SOCKET;
  static void sock_close(socket_t s) { closesocket(s); }
  static void sock_init()    { WSADATA w; WSAStartup(MAKEWORD(2,2), &w); }
  static void sock_cleanup() { WSACleanup(); }
  static void enable_ansi() {
      HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
      DWORD mode = 0;
      GetConsoleMode(h, &mode);
      SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
  }
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <unistd.h>
  using socket_t = int;
  static const socket_t BAD_SOCK = -1;
  static void sock_close(socket_t s) { ::close(s); }
  static void sock_init()    {}
  static void sock_cleanup() {}
  static void enable_ansi()  {}
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

static const int PORT   = 54321;
static const int FLOORS = 10;

struct DisplayState {
    int floor  = 1;
    int target = 1;
    std::string door = "CLOSED";
    std::vector<int> queue;
};

static DisplayState g_state;
static std::mutex   g_state_mtx;

static std::vector<int> parse_queue(const std::string& s) {
    std::vector<int> v;
    if (s.empty()) return v;
    std::istringstream iss{s};
    std::string tok;
    while (std::getline(iss, tok, ',')) {
        try { v.push_back(std::stoi(tok)); } catch (...) {}
    }
    return v;
}

static DisplayState parse_state(const std::string& line) {
    DisplayState s;
    std::istringstream iss{line};
    std::string field;
    while (std::getline(iss, field, '|')) {
        auto eq = field.find('=');
        if (eq == std::string::npos) continue;
        std::string key = field.substr(0, eq);
        std::string val = field.substr(eq + 1);
        if      (key == "FLOOR")  try { s.floor  = std::stoi(val); } catch (...) {}
        else if (key == "TARGET") try { s.target = std::stoi(val); } catch (...) {}
        else if (key == "DOOR")   s.door  = val;
        else if (key == "QUEUE")  s.queue = parse_queue(val);
    }
    return s;
}

static void render(const DisplayState& s) {
    printf("\033[2J\033[H");
    printf("  +--------------------------------+\n");
    printf("  |      ELEVATOR  SIMULATOR       |\n");
    printf("  +--------------------------------+\n");

    for (int f = FLOORS; f >= 1; f--) {
        const char* car = "           ";
        if (f == s.floor) {
            if      (s.door == "CLOSED")  car = "   [===]   ";
            else if (s.door == "OPENING") car = "   [= =]   ";
            else if (s.door == "OPEN")    car = "   [   ]   ";
            else if (s.door == "CLOSING") car = "   [= =]   ";
        }
        const char* marker = (f == s.target && f != s.floor) ? " <-" : "   ";
        printf("  | %2d |%s|%s |\n", f, car, marker);
    }

    printf("  +--------------------------------+\n");
    printf("  | Doors : %-7s               |\n", s.door.c_str());

    char qbuf[64] = "(empty)";
    if (!s.queue.empty()) {
        std::ostringstream oss;
        for (size_t i = 0; i < s.queue.size(); i++) {
            if (i) oss << ", ";
            oss << s.queue[i];
        }
        snprintf(qbuf, sizeof(qbuf), "%s", oss.str().c_str());
    }
    printf("  | Queue : %-22s|\n", qbuf);
    printf("  +--------------------------------+\n");
    fflush(stdout);
}

static void run_recv(socket_t sock, std::atomic<bool>& done) {
    char buf[512];
    std::string partial;

    while (!done) {
        int n = recv(sock, buf, (int)sizeof(buf) - 1, 0);
        if (n <= 0) { done = true; break; }
        buf[n] = '\0';
        partial += buf;

        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::string line = partial.substr(0, pos);
            partial.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;

            DisplayState s = parse_state(line);
            {
                std::lock_guard g{g_state_mtx};
                g_state = s;
            }
            render(s);
        }
    }
}

int main() {
    sock_init();
    enable_ansi();

    socket_t sock = BAD_SOCK;
    for (int attempt = 0; attempt < 30 && sock == BAD_SOCK; attempt++) {
        socket_t s = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port        = htons(PORT);
        if (connect(s, (sockaddr*)&addr, sizeof(addr)) == 0) {
            sock = s;
        } else {
            sock_close(s);
            if (attempt == 0)
                printf("Waiting for elevator_controller on port %d...\n", PORT);
            std::this_thread::sleep_for(1s);
        }
    }

    if (sock == BAD_SOCK) {
        printf("Could not connect to controller after 30 seconds.\n");
        sock_cleanup();
        return 1;
    }

    std::atomic<bool> done{false};
    std::thread t_recv(run_recv, sock, std::ref(done));

    t_recv.join();

    sock_close(sock);
    sock_cleanup();
    printf("\nDisconnected.\n");
}
