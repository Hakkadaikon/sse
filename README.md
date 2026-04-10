# SSE

A lightweight Server-Sent Events (SSE) server written in C with zero libc dependency. All system calls are made directly via x86-64 Linux syscalls through hand-written assembly wrappers.

## Features

- **Zero libc dependency** — custom types, string operations, memory management, and I/O via raw syscalls
- **SSE protocol stack** — event serialization, connection management, multi-client broadcast, and Last-Event-ID replay
- **epoll-based event loop** — non-blocking I/O for handling multiple concurrent SSE connections
- **TDD-tested** — 36 tests (31 unit + 5 integration) using Google Test

## Architecture

```
src/
├── arch/              # x86-64 Linux syscall wrappers (socket, epoll, send, recv, etc.)
│   └── linux/x86_64/  # Assembly syscall implementations
├── http/              # HTTP/1.1 request parser
├── sse/               # SSE protocol stack
│   ├── sse_event.*    # Event serialization (data/event/id/retry fields)
│   ├── sse_conn.*     # Per-client connection management
│   └── sse_stream.*   # Multi-client stream, broadcast, event queue
├── util/              # Utilities (types, string, logging, memory, signals)
└── main.c             # Sample SSE server
```

## Requirements

- Linux x86-64
- GCC (C2x standard)
- CMake >= 3.14
- [just](https://github.com/casey/just) (optional, for task runner)

## Building

### Release build

```bash
just release-build
# or manually:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Debug build

```bash
just debug-build
# or manually:
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Running

Start the SSE server on port 8080:

```bash
./build/sse
```

Connect with the included client script:

```bash
./scripts/sse_client.sh
```

Or connect with curl:

```bash
curl -N http://localhost:8080/events
```

### Example output

```
id:1
event:tick
data:count:1

id:2
event:tick
data:count:2

id:3
event:tick
data:count:3
```

The server broadcasts a `tick` event every 2 seconds to all connected clients. Each event includes an incrementing ID and a counter in the data field.

## Testing

Tests require Google Test (fetched automatically via CMake FetchContent).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run unit tests (31 tests)
./build/tests/unit_tests

# Run integration tests (5 tests)
./build/tests/integration_tests
```

### Test structure

```
tests/
├── unit/
│   ├── test_sse_event.cpp    # Event init, serialization, comment, response header
│   ├── test_sse_conn.cpp     # Connection lifecycle, Last-Event-ID, send
│   └── test_sse_stream.cpp   # Stream management, broadcast, replay
└── integration/
    └── test_sse_server.cpp   # End-to-end SSE via socketpair
```

## SSE Protocol

This implementation follows the [W3C Server-Sent Events](https://html.spec.whatwg.org/multipage/server-sent-events.html) specification.

### Event format

```
id:<event-id>\n
event:<event-type>\n
retry:<milliseconds>\n
data:<payload>\n
\n
```

- `data:` — Event payload (required). Multi-line data is split into multiple `data:` lines.
- `event:` — Event type name (optional, defaults to `message`).
- `id:` — Event ID for reconnection (optional).
- `retry:` — Client reconnection interval in milliseconds (optional).
- Lines starting with `:` are comments (used for keepalive).

### Response headers

```
HTTP/1.1 200 OK
Content-Type: text/event-stream
Cache-Control: no-cache
Connection: keep-alive
```

## Development

This project uses three Claude Code agent skills for development:

| Agent | Model | Role |
|---|---|---|
| `designer` | Opus | Protocol stack design from SSE specification |
| `tester` | Opus | TDD with Red-Green-Refactor (t-wada style) |
| `programmer` | Sonnet | Implementation from design / TDD Green phase |

### Formatting

```bash
just fmt
```

### Clean

```bash
just clean
```

## License

[MIT](LICENSE) — Copyright (c) 2026 Hakkadaikon
