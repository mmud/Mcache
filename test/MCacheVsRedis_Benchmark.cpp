#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include <cstring>
#include <cstdint>
#include <functional>
#include <algorithm>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// ============================================================
// MCache PROTOCOL CLIENT
// ============================================================
class MCacheClient {
    SOCKET m_fd = INVALID_SOCKET;
    std::vector<uint8_t> m_buf;
    std::vector<uint8_t> m_pipebuf;   // ADD: reusable pipeline buffer
    const char* m_host = nullptr;
    int m_port = 0;

public:
    bool connect(const char* host, int port) {
        m_host = host;
        m_port = port;
        m_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (m_fd == INVALID_SOCKET) return false;

        int yes = 1;
        setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes));
        int bufsize = 256 * 1024;
        setsockopt(m_fd, SOL_SOCKET, SO_SNDBUF, (char*)&bufsize, sizeof(bufsize));
        setsockopt(m_fd, SOL_SOCKET, SO_RCVBUF, (char*)&bufsize, sizeof(bufsize));

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host, &addr.sin_addr);
        return ::connect(m_fd, (sockaddr*)&addr, sizeof(addr)) == 0;
    }

    bool reconnect() {
        close();
        Sleep(100);
        return connect(m_host, m_port);
    }

    void close() {
        if (m_fd != INVALID_SOCKET) {
            closesocket(m_fd);
            m_fd = INVALID_SOCKET;
        }
    }

    bool is_connected() { return m_fd != INVALID_SOCKET; }

    // Build one command into a buffer (header + body)
    void build_cmd(const std::vector<std::string>& cmd, std::vector<uint8_t>& out) {
        // Build body first into temp
        m_buf.clear();
        uint32_t nstr = htonl((uint32_t)cmd.size());
        m_buf.insert(m_buf.end(), (uint8_t*)&nstr, (uint8_t*)&nstr + 4);
        for (const auto& s : cmd) {
            uint32_t len = htonl((uint32_t)s.size());
            m_buf.insert(m_buf.end(), (uint8_t*)&len, (uint8_t*)&len + 4);
            m_buf.insert(m_buf.end(), s.begin(), s.end());
        }
        // Append [4-byte length header] + [body] to output
        uint32_t total = htonl((uint32_t)m_buf.size());
        out.insert(out.end(), (uint8_t*)&total, (uint8_t*)&total + 4);
        out.insert(out.end(), m_buf.begin(), m_buf.end());
    }

    int send_cmd(const std::vector<std::string>& cmd) {
        m_pipebuf.clear();
        build_cmd(cmd, m_pipebuf);
        // Single send for header + body together
        if (write_all((char*)m_pipebuf.data(), m_pipebuf.size())) return -1;
        return 0;
    }

    int recv_response() {
        uint32_t net_len = 0;
        if (read_full((char*)&net_len, 4)) return -1;
        uint32_t len = ntohl(net_len);
        if (len > 32 * 1024 * 1024) return -1;
        std::vector<char> body(len);
        if (read_full(body.data(), len)) return -1;
        return 0;
    }

    int sync_cmd(const std::vector<std::string>& cmd) {
        if (send_cmd(cmd)) return -1;
        return recv_response();
    }

    // FIXED: Build ALL commands into ONE buffer, send ONCE
    int pipeline(const std::vector<std::vector<std::string>>& cmds) {
        // Build all commands into single buffer
        m_pipebuf.clear();
        m_pipebuf.reserve(cmds.size() * 64);  // estimate
        for (const auto& cmd : cmds) {
            build_cmd(cmd, m_pipebuf);
        }

        // ONE send for all commands
        if (write_all((char*)m_pipebuf.data(), m_pipebuf.size())) return -1;

        // Read all responses
        for (size_t i = 0; i < cmds.size(); i++) {
            if (recv_response()) return -1;
        }
        return 0;
    }

private:
    int read_full(char* buf, size_t n) {
        while (n > 0) {
            int rv = recv(m_fd, buf, (int)n, 0);
            if (rv <= 0) return -1;
            n -= rv;
            buf += rv;
        }
        return 0;
    }

    int write_all(const char* buf, size_t n) {
        while (n > 0) {
            int rv = send(m_fd, buf, (int)n, 0);
            if (rv <= 0) return -1;
            n -= rv;
            buf += rv;
        }
        return 0;
    }
};

// ============================================================
// REDIS RESP CLIENT
// ============================================================
class RedisClient {
    SOCKET m_fd = INVALID_SOCKET;
    std::string m_sendbuf;
    char m_rawbuf[8192];
    std::string m_recvbuf;
    const char* m_host = nullptr;
    int m_port = 0;

    bool fill_recv() {
        int rv = recv(m_fd, m_rawbuf, sizeof(m_rawbuf), 0);
        if (rv <= 0) return false;
        m_recvbuf.append(m_rawbuf, rv);
        return true;
    }

    bool read_line(std::string& line) {
        while (true) {
            size_t pos = m_recvbuf.find("\r\n");
            if (pos != std::string::npos) {
                line = m_recvbuf.substr(0, pos);
                m_recvbuf.erase(0, pos + 2);
                return true;
            }
            if (!fill_recv()) return false;
        }
    }

    bool ensure_bytes(size_t n) {
        while (m_recvbuf.size() < n) {
            if (!fill_recv()) return false;
        }
        return true;
    }

    int skip_response() {
        std::string line;
        if (!read_line(line) || line.empty()) return -1;
        switch (line[0]) {
        case '+': case '-': case ':': return 0;
        case '$': {
            int len = std::stoi(line.substr(1));
            if (len >= 0) {
                if (!ensure_bytes(len + 2)) return -1;
                m_recvbuf.erase(0, len + 2);
            }
            return 0;
        }
        case '*': {
            int cnt = std::stoi(line.substr(1));
            if (cnt < 0) return 0;
            for (int i = 0; i < cnt; i++) {
                if (skip_response() < 0) return -1;
            }
            return 0;
        }
        default: return -1;
        }
    }

public:
    bool connect(const char* host, int port) {
        m_host = host;
        m_port = port;
        m_recvbuf.clear();

        m_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (m_fd == INVALID_SOCKET) return false;

        int yes = 1;
        setsockopt(m_fd, IPPROTO_TCP, TCP_NODELAY, (char*)&yes, sizeof(yes));

        int bufsize = 256 * 1024;
        setsockopt(m_fd, SOL_SOCKET, SO_SNDBUF, (char*)&bufsize, sizeof(bufsize));
        setsockopt(m_fd, SOL_SOCKET, SO_RCVBUF, (char*)&bufsize, sizeof(bufsize));

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        inet_pton(AF_INET, host, &addr.sin_addr);
        return ::connect(m_fd, (sockaddr*)&addr, sizeof(addr)) == 0;
    }

    bool reconnect() {
        close();
        Sleep(100);
        return connect(m_host, m_port);
    }

    void close() {
        if (m_fd != INVALID_SOCKET) {
            closesocket(m_fd);
            m_fd = INVALID_SOCKET;
        }
    }

    bool is_connected() { return m_fd != INVALID_SOCKET; }

    void build_cmd(const std::vector<std::string>& cmd, std::string& out) {
        out += "*" + std::to_string(cmd.size()) + "\r\n";
        for (const auto& s : cmd) {
            out += "$" + std::to_string(s.size()) + "\r\n" + s + "\r\n";
        }
    }

    int send_cmd(const std::vector<std::string>& cmd) {
        m_sendbuf.clear();
        build_cmd(cmd, m_sendbuf);
        return write_all(m_sendbuf.c_str(), m_sendbuf.size());
    }

    int recv_response() { return skip_response(); }

    int sync_cmd(const std::vector<std::string>& cmd) {
        if (send_cmd(cmd)) return -1;
        return recv_response();
    }

    int pipeline(const std::vector<std::vector<std::string>>& cmds) {
        m_sendbuf.clear();
        for (const auto& cmd : cmds) build_cmd(cmd, m_sendbuf);
        if (write_all(m_sendbuf.c_str(), m_sendbuf.size())) return -1;
        for (size_t i = 0; i < cmds.size(); i++) {
            if (recv_response()) return -1;
        }
        return 0;
    }

private:
    int write_all(const char* buf, size_t n) {
        while (n > 0) {
            int rv = send(m_fd, buf, (int)n, 0);
            if (rv <= 0) return -1;
            n -= rv;
            buf += rv;
        }
        return 0;
    }
};

// ============================================================
// BENCHMARK FRAMEWORK
// ============================================================
using Clock = std::chrono::high_resolution_clock;
using CmdGen = std::function<std::vector<std::string>(int)>;

struct BenchResult {
    std::string name;
    int ops;
    double total_ms;
    double ops_per_sec;
    double avg_lat_us;
    bool ok;
};

// Sequential benchmark with error recovery
template<typename Client>
BenchResult bench_seq(Client& c, const std::string& name, int n, CmdGen gen) {
    BenchResult result = { name, n, 0, 0, 0, false };

    // warmup
    for (int i = 0; i < 50; i++) {
        if (c.sync_cmd(gen(i)) < 0) {
            printf("  [!] %s: warmup failed at i=%d\n", name.c_str(), i);
            return result;
        }
    }

    int completed = 0;
    auto t0 = Clock::now();
    for (int i = 0; i < n; i++) {
        if (c.sync_cmd(gen(i)) < 0) {
            printf("  [!] %s: failed at op %d/%d\n", name.c_str(), i, n);
            break;
        }
        completed++;
    }
    double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

    if (completed == 0) return result;

    result.ops = completed;
    result.total_ms = ms;
    result.ops_per_sec = completed / (ms / 1000.0);
    result.avg_lat_us = (ms * 1000.0) / completed;
    result.ok = (completed == n);
    return result;
}

// Pipeline benchmark with error recovery
template<typename Client>
BenchResult bench_pipe(Client& c, const std::string& name, int n, int batch, CmdGen gen) {
    BenchResult result = { name, n, 0, 0, 0, false };

    // warmup with small batch
    {
        std::vector<std::vector<std::string>> w;
        for (int i = 0; i < 20; i++) w.push_back(gen(i));
        if (c.pipeline(w) < 0) {
            printf("  [!] %s: warmup pipeline failed\n", name.c_str());
            return result;
        }
    }

    int completed = 0;
    auto t0 = Clock::now();
    for (int sent = 0; sent < n; ) {
        int count = (std::min)(batch, n - sent);
        std::vector<std::vector<std::string>> cmds;
        cmds.reserve(count);
        for (int i = 0; i < count; i++) {
            cmds.push_back(gen(sent + i));
        }
        if (c.pipeline(cmds) < 0) {
            printf("  [!] %s: pipeline failed at op %d/%d\n", name.c_str(), sent, n);
            break;
        }
        sent += count;
        completed = sent;
    }
    double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

    if (completed == 0) return result;

    result.ops = completed;
    result.total_ms = ms;
    result.ops_per_sec = completed / (ms / 1000.0);
    result.avg_lat_us = (ms * 1000.0) / completed;
    result.ok = (completed == n);
    return result;
}

// Large deletion benchmark
template<typename Client>
double bench_large_del(Client& c, int zset_size, CmdGen zadd_gen, const std::string& key) {
    // build with small batches to avoid overwhelming server
    const int batch = 50;
    for (int sent = 0; sent < zset_size; ) {
        int count = (std::min)(batch, zset_size - sent);
        std::vector<std::vector<std::string>> cmds;
        for (int i = 0; i < count; i++) cmds.push_back(zadd_gen(sent + i));
        if (c.pipeline(cmds) < 0) {
            printf("[!] Failed building large zset at %d/%d\n", sent, zset_size);
            return -1;
        }
        sent += count;
    }

    auto t0 = Clock::now();
    if (c.sync_cmd({ "del", key }) < 0) return -1;
    if (c.sync_cmd({ "set", "__ping__", "1" }) < 0) return -1;
    double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
    c.sync_cmd({ "del", "__ping__" });
    return us;
}

// Cleanup helper
template<typename Client>
void cleanup(Client& c, const std::string& prefix, int n) {
    const int batch = 50;
    for (int sent = 0; sent < n; ) {
        int count = (std::min)(batch, n - sent);
        std::vector<std::vector<std::string>> cmds;
        for (int i = 0; i < count; i++) {
            cmds.push_back({ "del", prefix + std::to_string(sent + i) });
        }
        c.pipeline(cmds);
        sent += count;
    }
}

void print_table(const std::string& label, const std::vector<BenchResult>& r) {
    printf("\n================== %s ==================\n", label.c_str());
    printf("%-32s %8s %12s %11s %6s\n", "Test", "Ops", "Ops/sec", "Avg Lat us", "OK?");
    printf("----------------------------------------------------------------------\n");
    for (const auto& b : r) {
        printf("%-32s %8d %12.0f %11.1f %6s\n",
            b.name.c_str(), b.ops, b.ops_per_sec, b.avg_lat_us,
            b.ok ? "YES" : "FAIL");
    }
}

void print_comparison(const std::vector<BenchResult>& MCache,
    const std::vector<BenchResult>& redis)
{
    printf("\n=================== COMPARISON ===================\n");
    printf("%-32s %11s %11s %8s\n", "Test", "MCache/s", "Redis/s", "Winner");
    printf("----------------------------------------------------------------------\n");
    size_t n = (std::min)(MCache.size(), redis.size());
    for (size_t i = 0; i < n; i++) {
        if (!MCache[i].ok || !redis[i].ok) {
            printf("%-32s %11s %11s %8s\n", MCache[i].name.c_str(),
                MCache[i].ok ? "ok" : "FAIL",
                redis[i].ok ? "ok" : "FAIL", "SKIP");
            continue;
        }
        double ratio = MCache[i].ops_per_sec / redis[i].ops_per_sec;
        printf("%-32s %11.0f %11.0f %7.2fx %s\n",
            MCache[i].name.c_str(),
            MCache[i].ops_per_sec,
            redis[i].ops_per_sec,
            ratio > 1.0 ? ratio : 1.0 / ratio,
            ratio > 1.0 ? "MCache" : "REDIS");
    }
}

// ============================================================
// CONNECTIVITY TEST
// ============================================================
template<typename Client>
bool verify_connection(Client& c, const std::string& label) {
    printf("  Verifying %s connection... ", label.c_str());
    if (c.sync_cmd({ "set", "__test__", "hello" }) < 0) {
        printf("FAILED (set)\n");
        return false;
    }
    if (c.sync_cmd({ "del", "__test__" }) < 0) {
        printf("FAILED (del)\n");
        return false;
    }
    printf("OK\n");
    return true;
}

// ============================================================
// MAIN
// ============================================================
int main(int argc, char** argv) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    int MCache_port = 1234;
    int redis_port = 6379;
    if (argc > 1) MCache_port = atoi(argv[1]);
    if (argc > 2) redis_port = atoi(argv[2]);

    // START SMALL — increase once things work
    const int N = 10000;
    const int PIPE_BATCH = 50;     // small batches to avoid overwhelming
    const int ZSET_SIZE = 2000;

    auto gen_set = [](int i) {
        return std::vector<std::string>{
            "set", "k:" + std::to_string(i), std::string(100, 'x')};
        };
    auto gen_get = [](int i) {
        return std::vector<std::string>{"get", "k:" + std::to_string(i)};
        };
    auto gen_del = [](int i) {
        return std::vector<std::string>{"del", "k:" + std::to_string(i)};
        };
    auto gen_zadd = [](int i) {
        return std::vector<std::string>{
            "zadd", "bz", std::to_string(i * 0.1), "m:" + std::to_string(i)};
        };
    auto gen_zadd_big = [](int i) {
        return std::vector<std::string>{
            "zadd", "bigz", std::to_string(i * 0.1), "m:" + std::to_string(i)};
        };

    printf("======================================\n");
    printf("  Cache Benchmark: MCache vs Redis\n");
    printf("  Ops per test: %d\n", N);
    printf("  Pipeline batch: %d\n", PIPE_BATCH);
    printf("  Value size: 100 bytes\n");
    printf("======================================\n");

    // ---- MCache ----
    std::vector<BenchResult> cr;
    double MCache_del_us = -1;
    {
        MCacheClient c;
        if (!c.connect("127.0.0.1", MCache_port)) {
            printf("\n[!] Cannot connect to MCache server on port %d\n", MCache_port);
        }
        else {
            printf("\n[+] Connected to MCache server :%d\n", MCache_port);

            if (!verify_connection(c, "MCache")) {
                printf("[!] Connection verification failed\n");
                goto skip_MCache;
            }

            // cleanup from previous runs
            printf("  Cleaning up old keys...\n");
            cleanup(c, "k:", N);
            c.sync_cmd({ "del", "bz" });
            c.sync_cmd({ "del", "bigz" });

            printf("  Running benchmarks...\n");

            cr.push_back(bench_seq(c, "SET sequential", N, gen_set));
            printf("    SET seq: %.0f ops/s\n", cr.back().ops_per_sec);

            cr.push_back(bench_seq(c, "GET sequential", N, gen_get));
            printf("    GET seq: %.0f ops/s\n", cr.back().ops_per_sec);

            cr.push_back(bench_pipe(c, "SET pipeline", N, PIPE_BATCH, gen_set));
            printf("    SET pipe: %.0f ops/s\n", cr.back().ops_per_sec);

            cr.push_back(bench_pipe(c, "GET pipeline", N, PIPE_BATCH, gen_get));
            printf("    GET pipe: %.0f ops/s\n", cr.back().ops_per_sec);

            // cleanup before ZADD
            c.sync_cmd({ "del", "bz" });
            cr.push_back(bench_seq(c, "ZADD sequential", N, gen_zadd));
            printf("    ZADD seq: %.0f ops/s\n", cr.back().ops_per_sec);

            c.sync_cmd({ "del", "bz" });
            cr.push_back(bench_pipe(c, "ZADD pipeline", N, PIPE_BATCH, gen_zadd));
            printf("    ZADD pipe: %.0f ops/s\n", cr.back().ops_per_sec);

            cr.push_back(bench_pipe(c, "DEL pipeline", N, PIPE_BATCH, gen_del));
            printf("    DEL pipe: %.0f ops/s\n", cr.back().ops_per_sec);

            // large deletion test
            printf("    Large ZSET deletion (%d members)... ", ZSET_SIZE);
            MCache_del_us = bench_large_del(c, ZSET_SIZE, gen_zadd_big, "bigz");
            if (MCache_del_us > 0) {
                printf("%.0f us\n", MCache_del_us);
            }
            else {
                printf("FAILED\n");
            }

            c.close();
            print_table("MCache SERVER", cr);
        }
    }
skip_MCache:

    // ---- REDIS ----
    std::vector<BenchResult> rr;
    double redis_del_us = -1;
    {
        RedisClient c;
        if (!c.connect("127.0.0.1", redis_port)) {
            printf("\n[!] Cannot connect to Redis on port %d\n", redis_port);
            printf("    Start Redis: redis-server\n");
            printf("    Or Docker:   docker run -p 6379:6379 redis\n");
        }
        else {
            printf("\n[+] Connected to Redis :%d\n", redis_port);

            if (!verify_connection(c, "Redis")) {
                printf("[!] Connection verification failed\n");
                goto skip_redis;
            }

            printf("  Cleaning up old keys...\n");
            cleanup(c, "k:", N);
            c.sync_cmd({ "del", "bz" });
            c.sync_cmd({ "del", "bigz" });

            printf("  Running benchmarks...\n");

            rr.push_back(bench_seq(c, "SET sequential", N, gen_set));
            printf("    SET seq: %.0f ops/s\n", rr.back().ops_per_sec);

            rr.push_back(bench_seq(c, "GET sequential", N, gen_get));
            printf("    GET seq: %.0f ops/s\n", rr.back().ops_per_sec);

            rr.push_back(bench_pipe(c, "SET pipeline", N, PIPE_BATCH, gen_set));
            printf("    SET pipe: %.0f ops/s\n", rr.back().ops_per_sec);

            rr.push_back(bench_pipe(c, "GET pipeline", N, PIPE_BATCH, gen_get));
            printf("    GET pipe: %.0f ops/s\n", rr.back().ops_per_sec);

            c.sync_cmd({ "del", "bz" });
            rr.push_back(bench_seq(c, "ZADD sequential", N, gen_zadd));
            printf("    ZADD seq: %.0f ops/s\n", rr.back().ops_per_sec);

            c.sync_cmd({ "del", "bz" });
            rr.push_back(bench_pipe(c, "ZADD pipeline", N, PIPE_BATCH, gen_zadd));
            printf("    ZADD pipe: %.0f ops/s\n", rr.back().ops_per_sec);

            rr.push_back(bench_pipe(c, "DEL pipeline", N, PIPE_BATCH, gen_del));
            printf("    DEL pipe: %.0f ops/s\n", rr.back().ops_per_sec);

            printf("    Large ZSET deletion (%d members)... ", ZSET_SIZE);
            redis_del_us = bench_large_del(c, ZSET_SIZE, gen_zadd_big, "bigz");
            if (redis_del_us > 0) {
                printf("%.0f us\n", redis_del_us);
            }
            else {
                printf("FAILED\n");
            }

            c.close();
            print_table("REDIS", rr);
        }
    }
skip_redis:

    // ---- COMPARISON ----
    if (!cr.empty() && !rr.empty()) {
        print_comparison(cr, rr);

        if (MCache_del_us > 0 && redis_del_us > 0) {
            printf("\n--- Large ZSET DEL (%d members) ---\n", ZSET_SIZE);
            printf("MCache: %10.0f us\n", MCache_del_us);
            printf("Redis:  %10.0f us\n", redis_del_us);
            double ratio = redis_del_us / MCache_del_us;
            printf("MCache is %.1fx %s for large key deletion\n",
                ratio > 1 ? ratio : 1.0 / ratio,
                ratio > 1 ? "faster" : "slower");
            printf("(Thread pool offloads destruction)\n");
        }
    }

    WSACleanup();
    return 0;
}