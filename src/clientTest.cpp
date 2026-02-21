#include <iostream>
#include <vector>
#include <string>
#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

// Bumping the limit to 32 MiB to match the server test case
const size_t k_max_msg = 32 << 20;

static void die(const char* m) {
    fprintf(stderr, "Error [%d]: %s\n", WSAGetLastError(), m);
    WSACleanup();
    exit(1);
}

// Helper: Ensure we send the entire buffer even if send() does a partial write
static int32_t write_all(SOCKET fd, const char* buf, size_t n) {
    while (n > 0) {
        int rv = send(fd, buf, (int)n, 0);
        if (rv <= 0) return -1;
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

// Helper: Ensure we read the exact number of bytes even if recv() does a partial read
static int32_t read_full(SOCKET fd, char* buf, size_t n) {
    while (n > 0) {
        int rv = recv(fd, buf, (int)n, 0);
        if (rv <= 0) return -1;
        n -= (size_t)rv;
        buf += rv;
    }
    return 0;
}

int main() {
    // 1. Initialize Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) die("WSAStartup");

    SOCKET fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == INVALID_SOCKET) die("socket()");

    // 2. Connect to local server
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(1234);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        die("connect()");
    }
    printf("Connected to server.\n");

    // 3. Prepare the test data
    std::vector<std::string> query_list = {
        "hello1",
        "hello2",
        "hello3",
        std::string(k_max_msg, 'z'), // The 32MB monster
        "hello5"
    };

    // 4. PHASE 1: Pipeline all requests (Send everything first)
    printf("Sending %zu requests in pipeline...\n", query_list.size());
    for (const std::string& s : query_list) {
        uint32_t len = (uint32_t)s.size();
        uint32_t net_len = htonl(len); // Network Byte Order

        // Send header
        if (write_all(fd, (char*)&net_len, 4)) die("send header");
        // Send body
        if (write_all(fd, s.data(), s.size())) die("send body");

        if (s.size() > 1000) {
            printf(" -> Sent large request (%zu bytes)\n", s.size());
        }
        else {
            printf(" -> Sent: %s\n", s.c_str());
        }
    }

    // 5. PHASE 2: Collect all responses
    printf("\nReading responses...\n");
    for (size_t i = 0; i < query_list.size(); ++i) {
        // Read header
        uint32_t net_len;
        if (read_full(fd, (char*)&net_len, 4)) die("read header");
        uint32_t len = ntohl(net_len);

        // Read body
        std::vector<char> rbuf(len);
        if (read_full(fd, rbuf.data(), len)) die("read body");

        if (len > 1000) {
            printf(" <- Received response %zu: %u bytes of 'z'\n", i + 1, len);
        }
        else {
            printf(" <- Received response %zu: %.*s\n", i + 1, (int)len, rbuf.data());
        }
    }

    // 6. Cleanup
    printf("\nTest complete.\n");
    closesocket(fd);
    WSACleanup();
    return 0;
}