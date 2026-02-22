#include <iostream>
#include <vector>
#include <string>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

static void die(const char* m) {
    fprintf(stderr, "[%d] %s\n", WSAGetLastError(), m);
    exit(1);
}

// Helper to ensure all bytes are sent
static int32_t write_all(SOCKET fd, const char* buf, size_t n) {
    while (n > 0) {
        int rv = send(fd, buf, (int)n, 0);
        if (rv <= 0) return -1;
        n -= rv;
        buf += rv;
    }
    return 0;
}

// Helper to ensure all bytes are read
static int32_t read_full(SOCKET fd, char* buf, size_t n) {
    while (n > 0) {
        int rv = recv(fd, buf, (int)n, 0);
        if (rv <= 0) return -1;
        n -= rv;
        buf += rv;
    }
    return 0;
}

static int32_t send_command(SOCKET fd, const std::vector<std::string>& cmd) {
    std::vector<uint8_t> payload;

    // 1. Pack the number of arguments
    uint32_t nstr = htonl((uint32_t)cmd.size());
    payload.insert(payload.end(), (uint8_t*)&nstr, (uint8_t*)&nstr + 4);

    // 2. Pack each argument (length + data)
    for (const std::string& s : cmd) {
        uint32_t p_len = htonl((uint32_t)s.size());
        payload.insert(payload.end(), (uint8_t*)&p_len, (uint8_t*)&p_len + 4);
        payload.insert(payload.end(), s.begin(), s.end());
    }

    // 3. Prepend the total message length
    uint32_t total_len = htonl((uint32_t)payload.size());
    if (write_all(fd, (char*)&total_len, 4)) return -1;
    if (write_all(fd, (char*)payload.data(), payload.size())) return -1;

    return 0;
}

static int32_t read_response(SOCKET fd) {
    // 1. Read total length
    uint32_t net_len;
    if (read_full(fd, (char*)&net_len, 4)) return -1;
    uint32_t len = ntohl(net_len);

    // 2. Read status (first 4 bytes of body)
    uint32_t net_status;
    if (read_full(fd, (char*)&net_status, 4)) return -1;
    uint32_t status = ntohl(net_status);

    // 3. Read data (remaining bytes)
    std::vector<char> data(len - 4);
    if (len > 4) {
        if (read_full(fd, data.data(), len - 4)) return -1;
    }

    printf("Status: %u, Data: %.*s\n", status, (int)data.size(), data.data());
    return 0;
}

int main() {
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in addr = { AF_INET, htons(1234) };
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    connect(fd, (sockaddr*)&addr, sizeof(addr));

    // TEST 1: SET
    printf("Testing SET key=val...\n");
    send_command(fd, { "set", "mykey", "hello world" });
    read_response(fd);

    // TEST 2: GET
    printf("Testing GET key...\n");
    send_command(fd, { "get", "mykey" });
    read_response(fd);

    // TEST 3: DEL
    printf("Testing DEL key...\n");
    send_command(fd, { "del", "mykey" });
    read_response(fd);

    // TEST 4: GET (Non-existent)
    printf("Testing GET after DEL...\n");
    send_command(fd, { "get", "mykey" });
    read_response(fd);

    closesocket(fd);
    WSACleanup();
    return 0;
}