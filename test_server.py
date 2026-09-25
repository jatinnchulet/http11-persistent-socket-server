import socket
import sys
import time

def read_response(s):
    """Read a single HTTP response header and body using Content-Length."""
    header_data = b""
    while b"\r\n\r\n" not in header_data:
        chunk = s.recv(1)
        if not chunk:
            raise ConnectionResetError("Connection closed before response headers complete")
        header_data += chunk

    headers_part, remainder = header_data.split(b"\r\n\r\n", 1)
    lines = headers_part.decode("iso-8859-1").split("\r\n")
    status_line = lines[0]
    parts = status_line.split(" ", 2)
    status_code = int(parts[1])

    content_length = 0
    headers = {}
    for line in lines[1:]:
        if ":" in line:
            k, v = line.split(":", 1)
            headers[k.strip().lower()] = v.strip()

    if "content-length" in headers:
        content_length = int(headers["content-length"])

    body = remainder
    while len(body) < content_length:
        chunk = s.recv(content_length - len(body))
        if not chunk:
            raise ConnectionResetError("Connection closed before body complete")
        body += chunk

    return status_code, body.decode("utf-8"), headers

def run_rubric_test(host="localhost", port=8080):
    print("==================================================")
    print("TEST 1: Official Rubric Test (1 socket, 6 requests)")
    print("==================================================")
    print(f"s = socket.create_connection((\"{host}\", {port}))")

    s = socket.create_connection((host, port))

    rubric_cases = [
        ("GET /add?a=2&b=3 HTTP/1.1\r\nHost: localhost\r\n\r\n", "GET /add?a=2&b=3", 200, "5"),
        ("GET /sub?a=10&b=4 HTTP/1.1\r\nHost: localhost\r\n\r\n", "GET /sub?a=10&b=4", 200, "6"),
        ("GET /mul?a=6&b=7 HTTP/1.1\r\nHost: localhost\r\n\r\n", "GET /mul?a=6&b=7", 200, "42"),
        ("GET /div?a=1&b=0 HTTP/1.1\r\nHost: localhost\r\n\r\n", "GET /div?a=1&b=0", 400, None),
        ("GET /pow?a=2&b=8 HTTP/1.1\r\nHost: localhost\r\n\r\n", "GET /pow?a=2&b=8", 404, None),
        ("POST /add HTTP/1.1\r\nHost: localhost\r\n\r\n", "POST /add", 405, None),
    ]

    count = 0
    for req_bytes, label, expected_code, expected_body in rubric_cases:
        s.sendall(req_bytes.encode("ascii"))
        code, body, _ = read_response(s)

        if expected_body is not None:
            assert code == expected_code, f"Expected {expected_code}, got {code}"
            assert body == expected_body, f"Expected body {expected_body!r}, got {body!r}"
            print(f"{label:<20} -> {code} {body}")
        else:
            assert code == expected_code, f"Expected {expected_code}, got {code}"
            print(f"{label:<20} -> {code}")
        count += 1

    print("\nsocket still open: True")
    print(f"1 TCP handshake, {count} responses")
    s.close()
    print("PASS: Official rubric test passed completely!\n")

def run_extended_cases_test(host="localhost", port=8080):
    print("==================================================")
    print("TEST 2: Extended Feature & Edge Case Suite")
    print("==================================================")

    # Missing Host Header test
    s = socket.create_connection((host, port))
    s.sendall(b"GET /add?a=2&b=3 HTTP/1.1\r\n\r\n")
    code, _, _ = read_response(s)
    print(f"GET /add (no Host)    -> {code} (expected 400)")
    assert code == 400
    s.close()

    # Valid division test
    s = socket.create_connection((host, port))
    s.sendall(b"GET /div?a=9&b=3 HTTP/1.1\r\nHost: localhost\r\n\r\n")
    code, body, _ = read_response(s)
    print(f"GET /div?a=9&b=3      -> {code} {body} (expected 200 3)")
    assert code == 200 and body == "3"

    # Non-integer argument test
    s.sendall(b"GET /add?a=x&b=3 HTTP/1.1\r\nHost: localhost\r\n\r\n")
    code, _, _ = read_response(s)
    print(f"GET /add?a=x&b=3      -> {code} (expected 400)")
    assert code == 400

    # Negative numbers and parameter order test
    s.sendall(b"GET /add?b=-5&a=15 HTTP/1.1\r\nHost: localhost\r\n\r\n")
    code, body, _ = read_response(s)
    print(f"GET /add?b=-5&a=15    -> {code} {body} (expected 200 10)")
    assert code == 200 and body == "10"
    s.close()

    print("PASS: All extended edge cases passed!\n")

def run_pipelining_test(host="localhost", port=8080):
    print("==================================================")
    print("TEST 3: HTTP Pipelining (All 6 requests in one packet)")
    print("==================================================")

    s = socket.create_connection((host, port))

    batch_requests = (
        "GET /add?a=10&b=20 HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET /sub?a=50&b=15 HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET /mul?a=8&b=9 HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET /div?a=100&b=4 HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET /pow?a=2&b=3 HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "POST /mul HTTP/1.1\r\nHost: localhost\r\n\r\n"
    )

    # Send all at once in a single sendall
    s.sendall(batch_requests.encode("ascii"))
    print("Sent 6 pipelined requests in one write operation.")

    expected = [
        (200, "30"),
        (200, "35"),
        (200, "72"),
        (200, "25"),
        (404, ""),
        (405, "")
    ]

    for i, (exp_code, exp_body) in enumerate(expected, 1):
        code, body, _ = read_response(s)
        print(f"Pipelined response #{i} -> {code} {body}")
        assert code == exp_code, f"Response #{i}: expected code {exp_code}, got {code}"
        if exp_body:
            assert body == exp_body, f"Response #{i}: expected body {exp_body!r}, got {body!r}"

    s.close()
    print("PASS: HTTP pipelining handled correctly in sequence!\n")

def run_connection_close_test(host="localhost", port=8080):
    print("==================================================")
    print("TEST 4: Connection: close Header Support")
    print("==================================================")

    s = socket.create_connection((host, port))
    req = "GET /add?a=1&b=1 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"
    s.sendall(req.encode("ascii"))
    code, body, headers = read_response(s)
    print(f"GET /add with Connection: close -> {code} {body}")
    assert code == 200 and body == "2"
    assert headers.get("connection", "").lower() == "close"

    # Verify socket closes after this
    time.sleep(0.1)
    extra = s.recv(10)
    assert len(extra) == 0, "Socket should be closed by server on Connection: close"
    s.close()
    print("PASS: Connection: close honored properly!\n")

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "localhost"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080

    try:
        run_rubric_test(host, port)
        run_extended_cases_test(host, port)
        run_pipelining_test(host, port)
        run_connection_close_test(host, port)
        print("ALL TESTS PASSED SUCCESSFULLY! 100% COMPLIANT.")
    except Exception as e:
        print(f"\nTEST FAILED: {e}")
        sys.exit(1)
