#include <iostream>
#include <vector>
#include <string>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// Match the server's Tag constants
enum {
    TAG_NIL = 0,
    TAG_ERR = 1,
    TAG_STR = 2,
    TAG_INT = 3,
    TAG_DBL = 4,
    TAG_ARR = 5,
};

static void die(const char* m) {
    fprintf(stderr, "[%d] %s\n", WSAGetLastError(), m);
    exit(1);
}

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

// Helper to parse the Tagged response body
static int32_t parse_response(const uint8_t* data, size_t len) {
    if (len < 1) return -1;

    uint8_t tag = data[0];
    const uint8_t* payload = &data[1];
    size_t p_len = len - 1;

    switch (tag) {
    case TAG_NIL:
        printf("(nil)\n");
        return 1;

    case TAG_ERR: {
        if (p_len < 8) return -1;
        uint32_t code;
        uint32_t msg_len;
        memcpy(&code, &payload[0], 4);
        memcpy(&msg_len, &payload[4], 4);
        code = ntohl(code);
        msg_len = ntohl(msg_len);
        printf("(error) [%u] %.*s\n", code, (int)msg_len, &payload[8]);
        return 1 + 8 + msg_len;
    }
    case TAG_STR: {
        if (p_len < 4) return -1;
        uint32_t s_len;
        memcpy(&s_len, payload, 4);
        s_len = ntohl(s_len);
        printf("(string) %.*s\n", (int)s_len, &payload[4]);
        return 1 + 4 + s_len;
    }
    case TAG_INT: {
        if (p_len < 8) return -1;
        int64_t val;
        memcpy(&val, payload, 8); // Note: server should ideally use htonll
        printf("(integer) %lld\n", val);
        return 1 + 8;
    }
    case TAG_ARR: {
        if (p_len < 4) return -1;
        uint32_t n;
        memcpy(&n, payload, 4);
        n = ntohl(n);
        printf("(array) len=%u\n", n);
        size_t offset = 1 + 4;
        for (uint32_t i = 0; i < n; ++i) {
            printf("  [%u] ", i);
            int32_t rv = parse_response(&data[offset], len - offset);
            if (rv < 0) return -1;
            offset += rv;
        }
        return (int32_t)offset;
    }
    default:
        printf("Unknown tag: %u\n", tag);
        return -1;
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

static int32_t read_response(SOCKET fd) {
    uint32_t net_len;
    if (read_full(fd, (char*)&net_len, 4)) return -1;
    uint32_t len = ntohl(net_len);

    std::vector<uint8_t> body(len);
    if (read_full(fd, (char*)body.data(), len)) return -1;

    if (parse_response(body.data(), len) < 0) {
        printf("Protocol error\n");
        return -1;
    }
    return 0;
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

    // --- TEST 1: SET Multiple Keys ---
    // Verifies do_set and hashtable insertion
    printf("Setting 'key1' and 'key2'...\n");
    send_command(fd, { "set", "key1", "value_alpha" });
    read_response(fd);
    send_command(fd, { "set", "key2", "value_beta" });
    read_response(fd);

    // --- TEST 2: GET Existing Key ---
    // Verifies do_get and TAG_STR response
    printf("\nGetting 'key1'...\n");
    send_command(fd, { "get", "key1" });
    read_response(fd);

    // --- TEST 3: KEYS (The Array Test) ---
    // Verifies do_keys, hm_foreach, and TAG_ARR parsing
    printf("\nListing all keys...\n");
    send_command(fd, { "keys" });
    read_response(fd);

    // --- TEST 4: DEL Existing Key ---
    // Verifies do_del and TAG_INT response
    printf("\nDeleting 'key1' (expect 1)...\n");
    send_command(fd, { "del", "key1" });
    read_response(fd);

    // --- TEST 5: GET Deleted Key ---
    // Verifies do_get returns TAG_NIL when key is missing
    printf("\nGetting 'key1' again (expect nil)...\n");
    send_command(fd, { "get", "key1" });
    read_response(fd);

    // --- TEST 6: DEL Non-existent Key ---
    // Verifies do_del returns 0 for missing keys
    printf("\nDeleting 'nonexistent' (expect 0)...\n");
    send_command(fd, { "del", "nonexistent" });
    read_response(fd);

    // --- TEST 7: KEYS after deletion ---
    printf("\nListing all keys (should only show 'key2')...\n");
    send_command(fd, { "keys" });
    read_response(fd);

    closesocket(fd);
    WSACleanup();
    printf("\nTesting complete.\n");
    return 0;
}