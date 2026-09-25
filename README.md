# HTTP/1.1 Persistent Calculator in C

A minimalist, high-performance HTTP/1.1 calculator server and client implemented in pure C with standard socket APIs (no external frameworks or dependencies).


---

## Features

- **Socket-Level HTTP/1.1 Keep-Alive**: Maintains persistent connections across sequential requests (`socket still open: True`).
- **Exact Request Framing & Boundary Handling**: Accurately detects `\r\n\r\n` and consumes `Content-Length` bytes so byte $n+1$ belongs to the subsequent request.
- **HTTP Pipelining**: Efficiently processes multiple back-to-back requests packed into a single TCP packet in exact order.
- **Complete Operation Coverage**:
  - `GET /add?a=2&b=3` $\rightarrow$ `200 5`
  - `GET /sub?a=10&b=4` $\rightarrow$ `200 6`
  - `GET /mul?a=6&b=7` $\rightarrow$ `200 42`
  - `GET /div?a=9&b=3` $\rightarrow$ `200 3`
- **Robust Error Handling**:
  - `GET /div?a=1&b=0` $\rightarrow$ `400 Bad Request` (division by zero)
  - `GET /add?a=x&b=3` $\rightarrow$ `400 Bad Request` (non-integer parameter)
  - `GET /add` (no `Host` header) $\rightarrow$ `400 Bad Request` (RFC 7230 Host requirement)
  - `GET /pow?a=2&b=8` $\rightarrow$ `404 Not Found` (unknown route)
  - `POST /add` $\rightarrow$ `405 Method Not Allowed` (non-GET method)
- **Stretch Goals Included**:
  - `Connection: close` header handling (gracefully responds and terminates the connection).
  - 10-second idle connection timeout via `select()`.
- **Cross-Platform**: Supports Windows Winsock (`ws2_32.lib`) and POSIX (Linux/macOS) with zero code modifications.

---

## File Overview

| File | Description |
|---|---|
| [`server.c`](file:///d:/client-server-calculator-NA/server.c) | Minimalist, clean C HTTP/1.1 server with persistent buffer parsing |
| [`client.c`](file:///d:/client-server-calculator-NA/client.c) | C client performing 1 TCP handshake and executing all 6 rubric queries over the same socket |
| [`test_server.py`](file:///d:/client-server-calculator-NA/test_server.py) | Automated test suite verifying the instructor's rubric, edge cases, and pipelining |
| [`build.bat`](file:///d:/client-server-calculator-NA/build.bat) | Windows build script for MSVC `cl.exe` |
| [`Makefile`](file:///d:/client-server-calculator-NA/Makefile) | Makefile for Linux/macOS using `gcc` or `clang` |

---

## How to Build

### Windows (MSVC)
Run the automated build script:
```bat
build.bat
```
This compiles `server.exe` and `client.exe` without warnings.

### Linux / macOS
```bash
make
```

---

## How to Run

### 1. Start the Server
```bash
# Windows
server.exe 8080

# Linux/macOS
./server 8080
```

### 2. Run the C Client Test
In a separate terminal:
```bash
# Windows
client.exe 127.0.0.1 8080

# Linux/macOS
./client 127.0.0.1 8080
```

**Output:**
```
Connected to 127.0.0.1:8080
s = socket.create_connection(("127.0.0.1", 8080))

GET /add?a=2&b=3     -> 200 5
GET /sub?a=10&b=4    -> 200 6
GET /mul?a=6&b=7     -> 200 42
GET /div?a=1&b=0     -> 400
GET /pow?a=2&b=8     -> 404
POST /add            -> 405

socket still open: True
1 TCP handshake, 6 responses
```

### 3. Run the Automated Python Test Suite
```bash
python test_server.py
```
Tests:

#### 1. Official grading rubric (1 socket, 6 requests)
```
==================================================
TEST 1: Official Rubric Test (1 socket, 6 requests)
==================================================
s = socket.create_connection(("localhost", 8080))
GET /add?a=2&b=3     -> 200 5
GET /sub?a=10&b=4    -> 200 6
GET /mul?a=6&b=7     -> 200 42
GET /div?a=1&b=0     -> 400
GET /pow?a=2&b=8     -> 404
POST /add            -> 405

socket still open: True
1 TCP handshake, 6 responses
PASS: Official rubric test passed completely!
```

#### 2. Extended edge cases (missing Host header, division by zero, non-integer inputs, negative numbers)
```
==================================================
TEST 2: Extended Feature & Edge Case Suite
==================================================
GET /add (no Host)    -> 400 (expected 400)
GET /div?a=9&b=3      -> 200 3 (expected 200 3)
GET /add?a=x&b=3      -> 400 (expected 400)
GET /add?b=-5&a=15    -> 200 10 (expected 200 10)
PASS: All extended edge cases passed!
```

#### 3. HTTP pipelining (all requests written in a single batch)
```
==================================================
TEST 3: HTTP Pipelining (All 6 requests in one packet)
==================================================
Sent 6 pipelined requests in one write operation.
Pipelined response #1 -> 200 30
Pipelined response #2 -> 200 35
Pipelined response #3 -> 200 72
Pipelined response #4 -> 200 25
Pipelined response #5 -> 404 
Pipelined response #6 -> 405 
PASS: HTTP pipelining handled correctly in sequence!
```

#### 4. Connection: close header processing
```
==================================================
TEST 4: Connection: close Header Support
==================================================
GET /add with Connection: close -> 200 2
PASS: Connection: close honored properly!

ALL TESTS PASSED SUCCESSFULLY!.
```
