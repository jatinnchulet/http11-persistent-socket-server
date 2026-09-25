#ifdef _WIN32
  #define _CRT_SECURE_NO_WARNINGS
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
  #include <sys/select.h>
  #include <strings.h>
  typedef int socket_t;
  #define CLOSE_SOCKET(s) close(s)
  #define IS_INVALID_SOCKET(s) ((s) < 0)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define DEFAULT_PORT 8080
#define BUFFER_SIZE 65536

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

/* Parse exact integer (optional +/- followed by digits only) */
static int parse_int(const char *s, long long *out) {
    if (!s || !*s) return 0;
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return 0;

    char *end = NULL;
    errno = 0;
    long long val = strtoll(s, &end, 10);
    if (errno != 0 || end == s) return 0;
    while (isspace((unsigned char)*end)) end++;
    if (*end != '\0') return 0;

    *out = val;
    return 1;
}

/* Extract query parameter value by key from query string (e.g. key="a" from "a=2&b=3") */
static int get_query_param(const char *query, const char *key, char *out, size_t out_len) {
    if (!query || !key || !out || out_len == 0) return 0;
    size_t klen = strlen(key);
    const char *p = query;

    while (*p) {
        if ((p == query || *(p - 1) == '&') && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            p += klen + 1;
            size_t i = 0;
            while (*p && *p != '&' && i + 1 < out_len) {
                out[i++] = *p++;
            }
            out[i] = '\0';
            return 1;
        }
        p++;
    }
    return 0;
}

/* Send a complete HTTP/1.1 response with proper headers */
static void send_response(socket_t sock, int status_code, const char *reason_phrase,
                          const char *body, int keep_alive) {
    char resp[1024];
    int body_len = body ? (int)strlen(body) : 0;
    const char *conn_header = keep_alive ? "keep-alive" : "close";

    int len = snprintf(resp, sizeof(resp),
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Length: %d\r\n"
                       "Content-Type: text/plain\r\n"
                       "Connection: %s\r\n"
                       "\r\n",
                       status_code, reason_phrase, body_len, conn_header);

    if (len > 0) {
        send(sock, resp, len, 0);
    }
    if (body_len > 0) {
        send(sock, body, body_len, 0);
    }
}

/* Parse and process a single HTTP request */
static void process_request(socket_t sock, const char *headers, int hdr_len,
                            const char *body, int body_len, int *should_close) {
    (void)body;
    (void)body_len;

    char hdr[4096];
    if (hdr_len >= (int)sizeof(hdr)) {
        send_response(sock, 400, "Bad Request", "", 0);
        *should_close = 1;
        return;
    }
    memcpy(hdr, headers, hdr_len);
    hdr[hdr_len] = '\0';

    char *first_line_end = strstr(hdr, "\r\n");
    if (!first_line_end) {
        send_response(sock, 400, "Bad Request", "", 0);
        *should_close = 1;
        return;
    }
    *first_line_end = '\0';

    /* Parse request line: METHOD URI VERSION */
    char method[16] = {0};
    char uri[2048] = {0};
    char version[16] = {0};

    if (sscanf(hdr, "%15s %2047s %15s", method, uri, version) < 3) {
        send_response(sock, 400, "Bad Request", "", 1);
        return;
    }

    /* Only GET is allowed for calculations */
    if (strcmp(method, "GET") != 0) {
        send_response(sock, 405, "Method Not Allowed", "", 1);
        return;
    }

    /* Split URI into path and query string */
    char path[512] = {0};
    char query[1536] = {0};
    char *qmark = strchr(uri, '?');
    if (qmark) {
        size_t plen = (size_t)(qmark - uri);
        if (plen >= sizeof(path)) plen = sizeof(path) - 1;
        memcpy(path, uri, plen);
        path[plen] = '\0';
        strncpy(query, qmark + 1, sizeof(query) - 1);
    } else {
        strncpy(path, uri, sizeof(path) - 1);
    }

    /* Validate path */
    int op = 0; /* 1: add, 2: sub, 3: mul, 4: div */
    if (strcmp(path, "/add") == 0) op = 1;
    else if (strcmp(path, "/sub") == 0) op = 2;
    else if (strcmp(path, "/mul") == 0) op = 3;
    else if (strcmp(path, "/div") == 0) op = 4;
    else {
        send_response(sock, 404, "Not Found", "", 1);
        return;
    }

    /* Extract parameters 'a' and 'b' */
    char a_str[64] = {0};
    char b_str[64] = {0};
    if (!get_query_param(query, "a", a_str, sizeof(a_str)) ||
        !get_query_param(query, "b", b_str, sizeof(b_str))) {
        send_response(sock, 400, "Bad Request", "", 1);
        return;
    }

    long long a = 0, b = 0;
    if (!parse_int(a_str, &a) || !parse_int(b_str, &b)) {
        send_response(sock, 400, "Bad Request", "", 1);
        return;
    }

    /* Calculate result */
    long long result = 0;
    switch (op) {
        case 1: result = a + b; break;
        case 2: result = a - b; break;
        case 3: result = a * b; break;
        case 4:
            if (b == 0) {
                send_response(sock, 400, "Bad Request", "", 1);
                return;
            }
            result = a / b;
            break;
    }

    char body_resp[64];
    snprintf(body_resp, sizeof(body_resp), "%lld", result);
    send_response(sock, 200, "OK", body_resp, 1);
}

static void handle_client(socket_t client_sock) {
    char buf[BUFFER_SIZE];
    int n = recv(client_sock, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        int should_close = 0;
        process_request(client_sock, buf, n, NULL, 0, &should_close);
    }
    CLOSE_SOCKET(client_sock);
}

int main(int argc, char *argv[]) {
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) {
            fprintf(stderr, "Invalid port number: %s\n", argv[1]);
            return 1;
        }
    }

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
#endif

    socket_t server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (IS_INVALID_SOCKET(server_sock)) {
        fprintf(stderr, "Failed to create socket\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    int opt = 1;
#ifdef _WIN32
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));
#else
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((unsigned short)port);

    if (bind(server_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "Failed to bind to port %d\n", port);
        CLOSE_SOCKET(server_sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    if (listen(server_sock, 128) != 0) {
        fprintf(stderr, "Failed to listen on socket\n");
        CLOSE_SOCKET(server_sock);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    printf("HTTP/1.1 Calculator Server listening on port %d...\n", port);
    fflush(stdout);

    for (;;) {
        struct sockaddr_in client_addr;
#ifdef _WIN32
        int client_len = sizeof(client_addr);
#else
        socklen_t client_len = sizeof(client_addr);
#endif
        socket_t client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &client_len);
        if (IS_INVALID_SOCKET(client_sock)) {
            continue;
        }
        handle_client(client_sock);
    }

    CLOSE_SOCKET(server_sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
