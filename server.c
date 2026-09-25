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
#define TIMEOUT_SECONDS 10

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

/* Check if request contains a valid, non-empty Host header */
static int has_valid_host_header(const char *headers_start) {
    const char *p = headers_start;
    while (p && *p) {
        while (*p == '\r' || *p == '\n') p++;
        if (!*p) break;
        if (ci_strstr(p, "Host:") == p) {
            p += 5;
            while (*p == ' ' || *p == '\t') p++;
            if (*p != '\r' && *p != '\n' && *p != '\0') {
                return 1;
            }
            return 0; /* Empty Host value */
        }
        const char *eol = strstr(p, "\r\n");
        if (!eol) eol = strchr(p, '\n');
        if (!eol) break;
        p = eol + (eol[0] == '\r' ? 2 : 1);
    }
    return 0;
}

/* Check if Connection: close is requested */
static int is_connection_close(const char *headers_start) {
    const char *p = headers_start;
    while (p && *p) {
        while (*p == '\r' || *p == '\n') p++;
        if (!*p) break;
        if (ci_strstr(p, "Connection:") == p) {
            p += 11;
            const char *eol = strstr(p, "\r\n");
            if (!eol) eol = strchr(p, '\n');
            size_t len = eol ? (size_t)(eol - p) : strlen(p);
            char val[64];
            if (len >= sizeof(val)) len = sizeof(val) - 1;
            memcpy(val, p, len);
            val[len] = '\0';
            if (ci_strstr(val, "close")) return 1;
        }
        const char *eol = strstr(p, "\r\n");
        if (!eol) eol = strchr(p, '\n');
        if (!eol) break;
        p = eol + (eol[0] == '\r' ? 2 : 1);
    }
    return 0;
}

/* Extract Content-Length from headers if present; returns 0 if absent, -1 if invalid */
static int extract_content_length(const char *headers, int hdr_len) {
    char temp[4096];
    if (hdr_len >= (int)sizeof(temp)) return -1;
    memcpy(temp, headers, hdr_len);
    temp[hdr_len] = '\0';

    const char *p = temp;
    while (p && *p) {
        while (*p == '\r' || *p == '\n') p++;
        if (!*p) break;
        if (ci_strstr(p, "Content-Length:") == p) {
            p += 15;
            while (*p == ' ' || *p == '\t') p++;
            long long cl = 0;
            char cl_buf[32];
            size_t i = 0;
            while (isdigit((unsigned char)*p) && i + 1 < sizeof(cl_buf)) {
                cl_buf[i++] = *p++;
            }
            cl_buf[i] = '\0';
            if (i == 0 || !parse_int(cl_buf, &cl) || cl < 0 || cl > 10000000) {
                return -1;
            }
            return (int)cl;
        }
        const char *eol = strstr(p, "\r\n");
        if (!eol) eol = strchr(p, '\n');
        if (!eol) break;
        p = eol + (eol[0] == '\r' ? 2 : 1);
    }
    return 0;
}

/* Send a complete HTTP response */
static void send_response(socket_t sock, int status_code, const char *status_phrase,
                          const char *body, int keep_alive) {
    char header_buf[512];
    size_t body_len = body ? strlen(body) : 0;

    int hdr_len = snprintf(header_buf, sizeof(header_buf),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "\r\n",
        status_code, status_phrase, body_len,
        keep_alive ? "keep-alive" : "close");

    if (hdr_len > 0) {
        send(sock, header_buf, hdr_len, 0);
    }
    if (body_len > 0) {
        send(sock, body, (int)body_len, 0);
    }
}

/* Process a single complete HTTP request */
static void process_request(socket_t sock, const char *headers, int hdr_len,
                            const char *body, int body_len, int *should_close) {
    (void)body;
    (void)body_len;

    /* Make a null-terminated copy of the header block for safe parsing */
    char hdr[8192];
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
    const char *headers_start = first_line_end + 2;

    /* Check Connection header */
    int keep_alive = 1;
    if (is_connection_close(headers_start)) {
        keep_alive = 0;
        *should_close = 1;
    }

    /* Parse request line: METHOD URI VERSION */
    char method[16] = {0};
    char uri[2048] = {0};
    char version[16] = {0};

    if (sscanf(hdr, "%15s %2047s %15s", method, uri, version) < 3) {
        send_response(sock, 400, "Bad Request", "", keep_alive);
        return;
    }

    /* Check HTTP method - only GET is allowed for calculation */
    if (strcmp(method, "GET") != 0) {
        send_response(sock, 405, "Method Not Allowed", "", keep_alive);
        return;
    }

    /* In HTTP/1.1, Host header is mandatory */
    if (strcmp(version, "HTTP/1.1") == 0) {
        if (!has_valid_host_header(headers_start)) {
            send_response(sock, 400, "Bad Request", "", keep_alive);
            return;
        }
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
        /* Unknown path (e.g. /pow) -> 404 */
        send_response(sock, 404, "Not Found", "", keep_alive);
        return;
    }

    /* Extract parameters 'a' and 'b' */
    char a_str[64] = {0};
    char b_str[64] = {0};
    if (!get_query_param(query, "a", a_str, sizeof(a_str)) ||
        !get_query_param(query, "b", b_str, sizeof(b_str))) {
        send_response(sock, 400, "Bad Request", "", keep_alive);
        return;
    }

    long long a = 0, b = 0;
    if (!parse_int(a_str, &a) || !parse_int(b_str, &b)) {
        send_response(sock, 400, "Bad Request", "", keep_alive);
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
                send_response(sock, 400, "Bad Request", "", keep_alive);
                return;
            }
            result = a / b;
            break;
    }

    char body_resp[64];
    snprintf(body_resp, sizeof(body_resp), "%lld", result);
    send_response(sock, 200, "OK", body_resp, keep_alive);
}

/* Handle client connection with persistent buffer and pipelining */
static void handle_client(socket_t client_sock) {
    char buf[BUFFER_SIZE];
    int buf_len = 0;

    for (;;) {
        /* Check if a complete request is in the buffer */
        char *hdr_end = NULL;
        for (int i = 0; i + 3 < buf_len; i++) {
            if (buf[i] == '\r' && buf[i+1] == '\n' && buf[i+2] == '\r' && buf[i+3] == '\n') {
                hdr_end = buf + i;
                break;
            }
        }

        if (hdr_end) {
            int hdr_len = (int)(hdr_end + 4 - buf);
            int content_len = extract_content_length(buf, hdr_len);
            if (content_len < 0) {
                send_response(client_sock, 400, "Bad Request", "", 0);
                break;
            }

            if (buf_len >= hdr_len + content_len) {
                int total_req_len = hdr_len + content_len;
                int should_close = 0;

                process_request(client_sock, buf, hdr_len, buf + hdr_len, content_len, &should_close);

                /* Shift remaining bytes in buffer forward */
                memmove(buf, buf + total_req_len, buf_len - total_req_len);
                buf_len -= total_req_len;

                if (should_close) break;

                /* Loop back immediately to handle any pipelined requests */
                continue;
            }
        }

        /* Buffer is full without a complete request */
        if (buf_len >= BUFFER_SIZE - 1) {
            send_response(client_sock, 400, "Bad Request", "", 0);
            break;
        }

        /* Wait for new data with timeout */
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(client_sock, &read_fds);

        struct timeval tv;
        tv.tv_sec = TIMEOUT_SECONDS;
        tv.tv_usec = 0;

        int sel = select((int)client_sock + 1, &read_fds, NULL, NULL, &tv);
        if (sel <= 0) {
            /* Timeout (10 seconds idle) or socket error */
            break;
        }

        int n = recv(client_sock, buf + buf_len, BUFFER_SIZE - 1 - buf_len, 0);
        if (n <= 0) {
            /* Client closed connection cleanly or network error */
            break;
        }
        buf_len += n;
        buf[buf_len] = '\0';
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
