#include <iostream>
#include <vector>
#include <string>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <assert.h>
#include <chrono>
#include <functional>
#include <iomanip>

#pragma comment(lib, "ws2_32.lib")

// Protocol Tags matching the server
enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

struct Response {
    uint8_t tag;
    int64_t int_val;
    double dbl_val;
    std::string str_val;
    std::vector<Response> arr_vals;
    uint32_t err_code;
};

struct BenchResult {
    long long set_time;
    long long get_time;
};

static void die(const char* m) {
    fprintf(stderr, "[%d] %s\n", WSAGetLastError(), m);
    exit(1);
}

// --- Network Helpers ---
static int32_t read_full(SOCKET fd, char* buf, size_t n) {
    while (n > 0) {
        int rv = recv(fd, buf, (int)n, 0);
        if (rv <= 0) return -1;
        n -= rv;
        buf += rv;
    }
    return 0;
}

static int32_t write_all(SOCKET fd, const char* buf, size_t n) {
    while (n > 0) {
        int rv = send(fd, buf, (int)n, 0);
        if (rv <= 0) return -1;
        n -= rv;
        buf += rv;
    }
    return 0;
}

static int32_t parse_response(const uint8_t* data, size_t len, Response& out) {
    if (len < 1) return -1;
    out.tag = data[0];
    const uint8_t* payload = &data[1];
    size_t p_len = len - 1;

    switch (out.tag) {
    case TAG_NIL: return 1;
    case TAG_ERR: {
        if (p_len < 8) return -1;
        uint32_t msg_len;
        memcpy(&out.err_code, &payload[0], 4);
        memcpy(&msg_len, &payload[4], 4);
        out.err_code = ntohl(out.err_code);
        msg_len = ntohl(msg_len);
        out.str_val.assign((char*)&payload[8], msg_len);
        return 1 + 8 + msg_len;
    }
    case TAG_STR: {
        if (p_len < 4) return -1;
        uint32_t s_len;
        memcpy(&s_len, payload, 4);
        s_len = ntohl(s_len);
        out.str_val.assign((char*)&payload[4], s_len);
        return 1 + 4 + s_len;
    }
    case TAG_INT: {
        if (p_len < 8) return -1;
        memcpy(&out.int_val, payload, 8);
        return 1 + 8;
    }
    case TAG_DBL: {
        if (p_len < 8) return -1;
        memcpy(&out.dbl_val, payload, 8);
        return 1 + 8;
    }
    case TAG_ARR: {
        if (p_len < 4) return -1;
        uint32_t n;
        memcpy(&n, payload, 4);
        n = ntohl(n);
        size_t offset = 1 + 4;
        out.arr_vals.clear();
        for (uint32_t i = 0; i < n; ++i) {
            Response item;
            int32_t rv = parse_response(&data[offset], len - offset, item);
            if (rv < 0) return -1;
            out.arr_vals.push_back(item);
            offset += rv;
        }
        return (int32_t)offset;
    }
    default: return -1;
    }
}

static int32_t send_command(SOCKET fd, const std::vector<std::string>& cmd) {
    std::vector<uint8_t> body;
    uint32_t nstr = htonl((uint32_t)cmd.size());
    body.insert(body.end(), (uint8_t*)&nstr, (uint8_t*)&nstr + 4);

    for (const std::string& s : cmd) {
        uint32_t p_len = htonl((uint32_t)s.size());
        body.insert(body.end(), (uint8_t*)&p_len, (uint8_t*)&p_len + 4);
        body.insert(body.end(), s.begin(), s.end());
    }

    uint32_t total_len = htonl((uint32_t)body.size());
    if (write_all(fd, (char*)&total_len, 4)) return -1;
    if (write_all(fd, (char*)body.data(), body.size())) return -1;
    return 0;
}

static Response sync_command(SOCKET fd, const std::vector<std::string>& cmd) {
    int32_t err = send_command(fd, cmd);
    assert(err == 0 && "Failed to send command");

    uint32_t net_len;
    err = read_full(fd, (char*)&net_len, 4);
    assert(err == 0 && "Failed to read response header");
    uint32_t len = ntohl(net_len);

    std::vector<uint8_t> body(len);
    err = read_full(fd, (char*)body.data(), len);
    assert(err == 0 && "Failed to read response body");

    Response res;
    int32_t consumed = parse_response(body.data(), len, res);
    assert(consumed > 0 && "Failed to parse protocol response");
    return res;
}

// --- Connection Helper ---
SOCKET connect_to_server(int port) {
    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) die("socket");

    sockaddr_in addr = { AF_INET, htons(port) };
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        fprintf(stderr, "Failed to connect to Port %d. Is the server running?\n", port);
        die("connect");
    }
    return fd;
}

// ==========================================
// ULTRA-FAST ATTACK PAYLOAD GENERATOR
// ==========================================
std::vector<std::string> generate_colliding_keys(size_t num_keys) {
    std::vector<std::string> malicious_keys;
    malicious_keys.reserve(num_keys);
    std::hash<std::string> hasher;

    // Target power-of-two bucket count common in standard libraries
    const size_t target_bucket_count = 65536;

    char buffer[64] = "atk_";
    char* num_start = buffer + 4;
    uint64_t counter = 0;

    // Reusable string to prevent reallocation during the brute-force phase
    std::string candidate;
    candidate.reserve(64);

    while (malicious_keys.size() < num_keys) {
        // Ultra-fast int-to-string (written backwards, but uniqueness is all that matters)
        uint64_t temp = counter++;
        char* p = num_start;
        do {
            *p++ = '0' + (temp % 10);
            temp /= 10;
        } while (temp > 0);

        // Update the candidate string without triggering new heap allocations
        candidate.assign(buffer, p - buffer);

        // Check for collision
        if ((hasher(candidate) % target_bucket_count) == 0) {
            malicious_keys.push_back(candidate);

            // Print progress so you know it hasn't frozen
            if (malicious_keys.size() % 1000 == 0) {
                std::cout << "    ...found " << malicious_keys.size() << "/" << num_keys << " keys\n";
            }
        }
    }
    return malicious_keys;
}

// ==========================================
// BENCHMARK ROUTINE
// ==========================================
BenchResult run_attack_benchmark(SOCKET fd, const std::string& server_name, const std::vector<std::string>& attack_keys) {
    using namespace std::chrono;
    BenchResult result;

    std::cout << "--> Attacking " << server_name << "...\n";

    // --- PHASE 1: Malicious Inserts ---
    auto start = high_resolution_clock::now();
    for (const auto& key : attack_keys) {
        sync_command(fd, { "set", key, "val" });
    }
    auto end = high_resolution_clock::now();
    result.set_time = duration_cast<milliseconds>(end - start).count();

    // --- PHASE 2: Malicious Lookups ---
    start = high_resolution_clock::now();
    for (const auto& key : attack_keys) {
        sync_command(fd, { "get", key });
    }
    end = high_resolution_clock::now();
    result.get_time = duration_cast<milliseconds>(end - start).count();

    std::cout << "    SET Time: " << result.set_time << " ms\n";
    std::cout << "    GET Time: " << result.get_time << " ms\n\n";

    return result;
}

int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) die("WSAStartup");

    const int NUM_OPERATIONS = 15000; // Increase this if the std::map is surviving too easily

    std::cout << "==================================================\n";
    std::cout << " HASHTABLE BENCHMARK (N=" << NUM_OPERATIONS << ")\n";
    std::cout << "==================================================\n\n";

    std::cout << "[1] Generating Payload...\n";
    std::vector<std::string> attack_keys = generate_colliding_keys(NUM_OPERATIONS);
    std::cout << "    Done.\n\n";

    std::cout << "[2] Connecting to servers...\n";
    SOCKET fd_MCache = connect_to_server(1234);
    std::cout << "    Connected to MCache Server (Port 1234)\n";

    SOCKET fd_std = connect_to_server(1235);
    std::cout << "    Connected to std::unordered_map Server (Port 1235)\n\n";

    std::cout << "[3] Running Benchmark...\n";
    BenchResult MCache_res = run_attack_benchmark(fd_MCache, "MCache HashTable (Port 1234)", attack_keys);
    BenchResult std_res = run_attack_benchmark(fd_std, "std::unordered_map (Port 1235)", attack_keys);

    // ==========================================
    // FINAL COMPARISON CALCULATION
    // ==========================================
    std::cout << "==================================================\n";
    std::cout << " FINAL RESULTS: SPEED COMPARISON\n";
    std::cout << "==================================================\n";

    if (MCache_res.get_time > 0 && MCache_res.set_time > 0) {
        double get_speedup = (double)std_res.get_time / MCache_res.get_time;
        double set_speedup = (double)std_res.set_time / MCache_res.set_time;

        std::cout << "[!] Lookups (GET): Your MCache HashTable was "
            << std::fixed << std::setprecision(2) << get_speedup << "x FASTER.\n";

        std::cout << "[!] Inserts (SET): Your MCache HashTable was "
            << std::fixed << std::setprecision(2) << set_speedup << "x FASTER.\n";
    }
    else {
        std::cout << "Your server was so fast the timer registered 0ms! Increase NUM_OPERATIONS.\n";
    }
    std::cout << "==================================================\n\n";

    std::cout << "Cleaning up (sending DEL commands)...\n";
    for (const auto& key : attack_keys) {
        sync_command(fd_MCache, { "del", key });
        sync_command(fd_std, { "del", key });
    }

    closesocket(fd_MCache);
    closesocket(fd_std);
    WSACleanup();

    return 0;
}