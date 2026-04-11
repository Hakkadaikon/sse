# SSE

A lightweight Server-Sent Events (SSE) server written in C with zero libc dependency. All system calls are made directly via x86-64 Linux syscalls through hand-written assembly wrappers.

## Features

- **Zero libc dependency** — custom types, string operations, memory management, and I/O via raw syscalls
- **SSE protocol stack** — event serialization, connection management, multi-client broadcast, and Last-Event-ID replay
- **epoll-based event loop** — non-blocking I/O for handling multiple concurrent SSE connections
- **Callback-based API** — simple high-level interface via `sse/sse.h` with generic `void*` user data
- **Graceful shutdown** — handles SIGINT/SIGTERM to clean up connections and file descriptors
- **TDD-tested** — 43 tests (31 unit + 5 integration + 7 API) using Google Test

## Architecture

```
src/
├── sse/               # SSE protocol stack
│   ├── sse.h / sse.c  # High-level server API (SSEServer, callbacks)
│   ├── sse_event.*    # Event serialization (data/event/id/retry fields)
│   ├── sse_conn.*     # Per-client connection management
│   └── sse_stream.*   # Multi-client stream, broadcast, event queue
├── arch/              # x86-64 Linux syscall wrappers (socket, epoll, send, recv, etc.)
│   └── linux/x86_64/  # Assembly syscall implementations
├── http/              # HTTP/1.1 request parser
├── util/              # Utilities (types, string, logging, memory, signals)
└── main.c             # Sample SSE server
```

## Requirements

- Linux x86-64
- GCC (C23 standard)
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

## API Usage

The `sse/sse.h` header provides a high-level, callback-based API for building SSE servers. Include only this one header — it pulls in everything you need.

### Quick start

```c
#include "sse/sse.h"
#include "util/allocator.h"  /* _memcpy, _memset */
#include "util/string.h"     /* _itoa, _strnlen  */

static bool on_event(SSEEvent* event, void* user_data)
{
  /* Fill in the event fields — return true to broadcast, false to skip */
  _memcpy(event->data, "hello", 5);
  _memcpy(event->event_type, "greeting", 8);
  return true;
}

int main(void)
{
  SSEServerConfig config = {
    .port              = 8080,
    .backlog           = 128,
    .event_interval_ms = 2000  /* call on_event every 2 seconds */
  };

  SSEServer server;
  sse_server_init(&server, &config);
  sse_server_on_event(&server, on_event, NULL);
  sse_server_run(&server);     /* blocks until SIGINT/SIGTERM */
  sse_server_cleanup(&server);
  return 0;
}
```

### API reference

#### Configuration

```c
typedef struct {
  uint16_t port;              /* TCP port to listen on */
  int32_t  backlog;           /* listen() backlog (0 defaults to 128) */
  int32_t  event_interval_ms; /* epoll timeout / event callback interval */
} SSEServerConfig;
```

#### Callbacks

| Callback type | Signature | Purpose |
|---|---|---|
| `SSEEventCallback` | `bool (*)(SSEEvent* event, void* user_data)` | Called periodically. Fill `event` fields and return `true` to broadcast, `false` to skip. |
| `SSEConnectCallback` | `void (*)(int32_t client_fd, void* user_data)` | Called when a new client connects (optional). |
| `SSEDisconnectCallback` | `void (*)(int32_t client_fd, void* user_data)` | Called when a client disconnects (optional). |

All callbacks receive the same `void* user_data` pointer registered with `sse_server_on_event`, allowing any application state to be passed through.

#### Functions

| Function | Description |
|---|---|
| `sse_server_init(server, config)` | Initialize server: create socket, bind, listen, set up epoll, register signal handlers. |
| `sse_server_on_event(server, cb, user_data)` | Register the event callback with a user data pointer. |
| `sse_server_on_connect(server, cb)` | Register an optional connect callback. |
| `sse_server_on_disconnect(server, cb)` | Register an optional disconnect callback. |
| `sse_server_run(server)` | Run the event loop (blocking). Returns on SIGINT/SIGTERM or `sse_server_stop()`. |
| `sse_server_stop(server)` | Signal the event loop to stop. |
| `sse_server_cleanup(server)` | Close all connections and file descriptors. |

#### SSEEvent fields

```c
typedef struct {
  char    data[4096];        /* Event payload (required for broadcast) */
  char    event_type[64];    /* Event type name (optional, "message" if empty) */
  char    id[64];            /* Event ID for Last-Event-ID (optional) */
  int32_t retry_ms;          /* Reconnection interval hint (optional, -1 = unset) */
} SSEEvent;
```

### Example: counter with connect/disconnect logging

```c
#include "sse/sse.h"
#include "util/allocator.h"
#include "util/string.h"

typedef struct {
  uint32_t counter;
  uint32_t clients;
} AppState;

static bool on_tick(SSEEvent* event, void* user_data)
{
  AppState* state = (AppState*)user_data;
  state->counter++;

  char buf[16];
  _memset(buf, 0, sizeof(buf));
  _itoa(state->counter, buf, sizeof(buf));

  size_t len = _strnlen(buf, sizeof(buf));
  _memcpy(event->data, "count:", 6);
  _memcpy(event->data + 6, buf, len);
  _memcpy(event->event_type, "tick", 4);
  _memcpy(event->id, buf, len);

  return true;
}

static void on_connect(int32_t fd, void* user_data)
{
  AppState* state = (AppState*)user_data;
  state->clients++;
}

static void on_disconnect(int32_t fd, void* user_data)
{
  AppState* state = (AppState*)user_data;
  state->clients--;
}

int main(void)
{
  SSEServerConfig config = {
    .port              = 8080,
    .backlog           = 128,
    .event_interval_ms = 2000
  };

  SSEServer server;
  static AppState state = {0, 0};

  sse_server_init(&server, &config);
  sse_server_on_event(&server, on_tick, &state);
  sse_server_on_connect(&server, on_connect);
  sse_server_on_disconnect(&server, on_disconnect);
  sse_server_run(&server);
  sse_server_cleanup(&server);

  return 0;
}
```

### Client connection

```bash
# Using the included script
./scripts/sse_client.sh

# Or with curl
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

## Testing

Tests require Google Test (fetched automatically via CMake FetchContent).

```bash
just test              # Run all tests (unit + integration + api)
just unit-test         # Run unit tests only (31 tests)
just integration-test  # Run integration tests only (5 tests)
just api-test          # Run API tests only (7 tests)
```

Or manually:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/tests/unit_tests
./build/tests/integration_tests
./build/tests/api_tests
```

### Test structure

```
tests/
├── unit/
│   ├── test_sse_event.cpp       # Event init, serialization, comment, response header
│   ├── test_sse_conn.cpp        # Connection lifecycle, Last-Event-ID, send
│   ├── test_sse_stream.cpp      # Stream management, broadcast, replay
│   └── test_sse_server_api.cpp  # High-level SSE server API
└── integration/
    └── test_sse_server.cpp      # End-to-end SSE via socketpair
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
