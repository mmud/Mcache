#include <iostream>
#include <vector>
#include <string>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <assert.h>
#include <cmath>
#include <windows.h>

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

// Structure to capture server response for assertions
struct Response {
    uint8_t tag;
    int64_t int_val;
    double dbl_val;
    std::string str_val;
    std::vector<Response> arr_vals;
    uint32_t err_code;
};

// Error handling helper
static void die(const char* m) {
    fprintf(stderr, "[%d] %s\n", WSAGetLastError(), m);
    exit(1);
}

// Ensure full read from socket
static int32_t read_full(SOCKET fd, char* buf, size_t n) {
    while (n > 0) {
        int rv = recv(fd, buf, (int)n, 0);
        if (rv <= 0) return -1;
        n -= rv;
        buf += rv;
    }
    return 0;
}

// Ensure full write to socket
static int32_t write_all(SOCKET fd, const char* buf, size_t n) {
    while (n > 0) {
        int rv = send(fd, buf, (int)n, 0);
        if (rv <= 0) return -1;
        n -= rv;
        buf += rv;
    }
    return 0;
}

// Recursive parser for tagged responses
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

// Serializes command to server format
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

// Synchronous request-response helper for tests
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

int main() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) die("WSAStartup");

    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET) die("socket");

    sockaddr_in addr = { AF_INET, htons(1234) };
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        die("connect");
    }

    printf("--- STARTING ALL UNIT TESTS ---\n");

    // ==========================================
    // PART 1: HASHTABLE TESTS (SET, GET, KEYS, DEL)
    // ==========================================
    printf("[1/2] Testing Hashtable operations...\n");

    // SET/GET
    sync_command(fd, { "set", "k1", "v1" });
    Response r_get = sync_command(fd, { "get", "k1" });
    assert(r_get.tag == TAG_STR && r_get.str_val == "v1");

    // KEYS
    Response r_keys = sync_command(fd, { "keys" });
    assert(r_keys.tag == TAG_ARR);
    bool found = false;
    for (const auto& r : r_keys.arr_vals) if (r.str_val == "k1") found = true;
    assert(found && "Key 'k1' not found in KEYS");

    // DEL
    Response r_del = sync_command(fd, { "del", "k1" });
    assert(r_del.tag == TAG_INT && r_del.int_val == 1);
    assert(sync_command(fd, { "get", "k1" }).tag == TAG_NIL);
    printf("OK: Hashtable tests passed.\n");

    // ==========================================
    // PART 2: SORTED SET TESTS (ZADD, ZSCORE, ZQUERY, ZREM)
    // ==========================================
    printf("[2/2] Testing Sorted Set operations...\n");

    // zscore on empty key
    assert(sync_command(fd, { "zscore", "empty_z", "n1" }).tag == TAG_NIL);

    // zquery on empty key
    Response r_zq_empty = sync_command(fd, { "zquery", "empty_z", "1", "", "0", "10" });
    assert(r_zq_empty.tag == TAG_ARR && r_zq_empty.arr_vals.empty());

    // zadd (Add new)
    assert(sync_command(fd, { "zadd", "zset", "1.0", "n1" }).int_val == 1);
    assert(sync_command(fd, { "zadd", "zset", "2.0", "n2" }).int_val == 1);

    // zadd (Update score)
    assert(sync_command(fd, { "zadd", "zset", "1.1", "n1" }).int_val == 0);

    // zscore check
    Response r_zs = sync_command(fd, { "zscore", "zset", "n1" });
    assert(r_zs.tag == TAG_DBL && std::abs(r_zs.dbl_val - 1.1) < 1e-9);

    // zquery full range
    Response r_zq = sync_command(fd, { "zquery", "zset", "1.0", "", "0", "10" });
    assert(r_zq.tag == TAG_ARR && r_zq.arr_vals.size() == 4);
    assert(r_zq.arr_vals[0].str_val == "n1"); // member 1
    assert(std::abs(r_zq.arr_vals[1].dbl_val - 1.1) < 1e-9); // score 1
    assert(r_zq.arr_vals[2].str_val == "n2"); // member 2

    // zquery with offset/pagination
    Response r_zq_off = sync_command(fd, { "zquery", "zset", "1.1", "", "1", "10" });
    assert(r_zq_off.arr_vals.size() == 2);
    assert(r_zq_off.arr_vals[0].str_val == "n2");

    // zrem
    assert(sync_command(fd, { "zrem", "zset", "n1" }).int_val == 1);
    assert(sync_command(fd, { "zscore", "zset", "n1" }).tag == TAG_NIL);
    printf("OK: Sorted Set tests passed.\n");

    // Error Handling
    assert(sync_command(fd, { "unknown_command" }).tag == TAG_ERR);

    // ==========================================
// PART 3: TTL TESTS (PEXPIRE, PTTL)
// ==========================================
    printf("[3/3] Testing TTL operations...\n");

    // Set key
    sync_command(fd, { "set", "ttl_key", "hello" });

    // Set expiration 2000 ms
    Response r_exp = sync_command(fd, { "pexpire", "ttl_key", "2000" });
    assert(r_exp.tag == TAG_INT && r_exp.int_val == 1);

    // Immediately check TTL (should be >0 and <=2000)
    Response r_ttl = sync_command(fd, { "pttl", "ttl_key" });
    assert(r_ttl.tag == TAG_INT);
    assert(r_ttl.int_val > 0 && r_ttl.int_val <= 2000);

    // Wait 3 seconds (3000 ms)
    Sleep(3000);

    // Now key should be expired
    Response r_get_expired = sync_command(fd, { "get", "ttl_key" });
    assert(r_get_expired.tag == TAG_NIL);

    // PTTL should return -2 (key does not exist)
    Response r_ttl_expired = sync_command(fd, { "pttl", "ttl_key" });
    assert(r_ttl_expired.tag == TAG_INT && r_ttl_expired.int_val == -2);

    printf("OK: Basic TTL expiration test passed.\n");

    // ------------------------------------------
    // Test updating TTL
    // ------------------------------------------
    sync_command(fd, { "set", "ttl_key2", "abc" });

    // Set TTL 1000 ms
    sync_command(fd, { "pexpire", "ttl_key2", "1000" });

    // Update TTL to 3000 ms
    sync_command(fd, { "pexpire", "ttl_key2", "3000" });

    // Sleep 1500 ms (should NOT expire yet)
    Sleep(1500);

    // Key must still exist
    Response r_still_exists = sync_command(fd, { "get", "ttl_key2" });
    assert(r_still_exists.tag == TAG_STR && r_still_exists.str_val == "abc");

    // Sleep additional 2000 ms (total >3000)
    Sleep(2000);

    // Now must be expired
    assert(sync_command(fd, { "get", "ttl_key2" }).tag == TAG_NIL);

    printf("OK: TTL update test passed.\n");

    // ------------------------------------------
    // Test TTL removed when key deleted
    // ------------------------------------------
    sync_command(fd, { "set", "ttl_key3", "xyz" });
    sync_command(fd, { "pexpire", "ttl_key3", "5000" });

    // Delete manually
    sync_command(fd, { "del", "ttl_key3" });

    // Should not exist
    assert(sync_command(fd, { "get", "ttl_key3" }).tag == TAG_NIL);

    // PTTL should return -2
    Response r_ttl_deleted = sync_command(fd, { "pttl", "ttl_key3" });
    assert(r_ttl_deleted.tag == TAG_INT && r_ttl_deleted.int_val == -2);

    printf("OK: TTL delete interaction test passed.\n");

    printf("OK: TTL tests passed.\n");

    printf("[ThreadPool] Testing async large ZSET deletion...\n");

    // Create large zset
    for (int i = 0; i < 2000; ++i) {
        sync_command(fd, {
            "zadd", "big_zset",
            std::to_string(i * 1.0),
            "member_" + std::to_string(i)
            });
    }

    // Delete the whole key (should trigger async free)
    sync_command(fd, { "del", "big_zset" });

    // Immediately issue another command to check server responsiveness
    Response r_ping = sync_command(fd, { "set", "after_big_delete", "ok" });
    assert(r_ping.tag == TAG_NIL || r_ping.tag == TAG_STR);

    // Ensure server didn't crash
    assert(sync_command(fd, { "get", "after_big_delete" }).tag == TAG_STR);

    printf("OK: Async large delete did not block server.\n");

    printf("[ThreadPool] Testing async expiration of large ZSET...\n");

    // Create large zset
    for (int i = 0; i < 2000; ++i) {
        sync_command(fd, {
            "zadd", "big_ttl_zset",
            std::to_string(i * 1.0),
            "m_" + std::to_string(i)
            });
    }

    // Set short TTL
    sync_command(fd, { "pexpire", "big_ttl_zset", "1000" });

    // Wait for expiration
    Sleep(1500);

    // Trigger active expiration by sending any command
    sync_command(fd, { "set", "trigger", "x" });

    // Ensure expired
    assert(sync_command(fd, { "get", "big_ttl_zset" }).tag == TAG_NIL);

    printf("OK: Async TTL expiration worked.\n");

    printf("[ThreadPool] Stress expiration burst...\n");

    for (int k = 0; k < 20; ++k) {
        std::string key = "burst_" + std::to_string(k);
        for (int i = 0; i < 1500; ++i) {
            sync_command(fd, {
                "zadd", key,
                std::to_string(i),
                "m_" + std::to_string(i)
                });
        }
        sync_command(fd, { "pexpire", key, "1000" });
    }

    Sleep(1500);

    // Trigger expiration
    sync_command(fd, { "set", "trigger2", "x" });

    // Ensure server still alive
    assert(sync_command(fd, { "get", "trigger2" }).tag == TAG_STR);

    printf("OK: Burst expiration did not crash.\n");

    sync_command(fd, { "del", "k1" });
    sync_command(fd, { "del", "zset" });
    sync_command(fd, { "del", "empty_z" });
    sync_command(fd, { "del", "ttl_key" });
    sync_command(fd, { "del", "ttl_key2" });
    sync_command(fd, { "del", "ttl_key3" });
    sync_command(fd, { "del", "big_zset" });
    sync_command(fd, { "del", "big_ttl_zset" });
    sync_command(fd, { "del", "after_big_delete" });
    sync_command(fd, { "del", "trigger" });
    sync_command(fd, { "del", "trigger2" });
    for (int k = 0; k < 20; ++k) {
        sync_command(fd, { "del", "burst_" + std::to_string(k) });
    }
    printf("Cleanup done.\n");

    closesocket(fd);
    WSACleanup();
    printf("\n--- [SUCCESS] ALL INTEGRATION CASES PASSED ---\n");
    return 0;
}