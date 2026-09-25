#ifdef _WIN32
  #define _CRT_SECURE_NO_WARNINGS
  #define _WINSOCK_DEPRECATED_NO_WARNINGS
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #pragma comment(lib, "ws2_32.lib")
  typedef SOCKET socket_t;
  #define CLOSE_SOCKET(s) closesocket(s)
  #define IS_INVALID_SOCKET(s) ((s) == INVALID_SOCKET)
#else
  #include <unistd.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
  typedef int socket_t;
  #define CLOSE_SOCKET(s) close(s)
  #define IS_INVALID_SOCKET(s) ((s) < 0)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Case-insensitive substring search */
static const char *ci_strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    size_t nlen = strlen(needle);
    if (nlen == 0) return haystack;
    while (*haystack) {
        #ifdef _WIN32
        if (_strnicmp(haystack, needle, nlen) == 0) return haystack;
        #else
        if (strncasecmp(haystack, needle, nlen) == 0) return haystack;
        #endif
        haystack++;
    }
    return NULL;
}

/* Helper to receive exactly 1 byte */
static int recv_byte(socket_t sock, char *ch) {
    int n = recv(sock, ch, 1, 0);
    return n == 1;
}

/* Read one HTTP response from the open socket */
static int read_http_response(socket_t sock, int *status_code, char *body_out, size_t max_body) {
    char header_buf[4096];
    size_t hlen = 0;

    /* Read headers byte-by-byte until \r\n\r\n */
    while (hlen + 1 < sizeof(header_buf)) {
        char ch;
        if (!recv_byte(sock, &ch)) {
            return 0; /* Socket closed prematurely */
        }
        header_buf[hlen++] = ch;
        header_buf[hlen] = '\0';
        if (hlen >= 4 &&
            header_buf[hlen - 4] == '\r' && header_buf[hlen - 3] == '\n' &&
            header_buf[hlen - 2] == '\r' && header_buf[hlen - 1] == '\n') {
            break;
        }
    }

    /* Parse status code from "HTTP/1.1 200 OK\r\n" */
    *status_code = 0;
    sscanf(header_buf, "%*s %d", status_code);

    /* Parse Content-Length */
    int content_length = 0;
    const char *cl_pos = ci_strstr(header_buf, "Content-Length:");
    if (cl_pos) {
        cl_pos += 15;
        content_length = atoi(cl_pos);
    }

    /* Read exactly Content-Length bytes */
    size_t blen = 0;
    for (int i = 0; i < content_length; i++) {
        char ch;
        if (!recv_byte(sock, &ch)) {
            return 0;
        }
        if (blen + 1 < max_body) {
            body_out[blen++] = ch;
        }
    }
    body_out[blen] = '\0';

    return 1;
}

int main(int argc, char *argv[]) {
    const char *host = "127.0.0.1";
    int port = 8080;

    if (argc > 1) host = argv[1];
    if (argc > 2) port = atoi(argv[2]);

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (IS_INVALID_SOCKET(sock)) {
        fprintf(stderr, "Failed to create socket\n");
        return 1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons((unsigned short)port);
    serv_addr.sin_addr.s_addr = inet_addr(host);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
        fprintf(stderr, "Failed to connect to %s:%d\n", host, port);
        CLOSE_SOCKET(sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    printf("Connected to %s:%d\n", host, port);

    /* Send test request */
    const char *test_req = "GET /add?a=2&b=3 HTTP/1.1\r\nHost: localhost\r\n\r\n";
    send(sock, test_req, (int)strlen(test_req), 0);

    int status = 0;
    char body[256] = {0};
    if (read_http_response(sock, &status, body, sizeof(body))) {
        printf("Response: %d %s\n", status, body);
    }

    CLOSE_SOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
