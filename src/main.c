/*
 * Sample SSE server
 *
 * Listens on port 8080, accepts connections, and sends SSE events
 * every 2 seconds to all connected clients.
 *
 * Usage:
 *   curl -N http://localhost:8080/events
 */

#include "sse/sse.h"
#include "util/allocator.h"
#include "util/string.h"

static bool on_tick(SSEEvent* event, void* user_data)
{
  uint32_t* counter = (uint32_t*)user_data;
  (*counter)++;

  char buf[16];
  _memset(buf, 0, sizeof(buf));
  itoa(*counter, buf, sizeof(buf));

  size_t buf_len = sse_strnlen(buf, sizeof(buf));

  _memcpy(event->data, "count:", 6);
  _memcpy(event->data + 6, buf, buf_len);
  _memcpy(event->event_type, "tick", 4);
  _memcpy(event->id, buf, buf_len);

  return true;
}

int main(void)
{
  SSEServerConfig config = {
    .port              = 8080,
    .backlog           = 128,
    .event_interval_ms = 2000};

  SSEServer       server;
  static uint32_t counter = 0;

  sse_server_init(&server, &config);
  sse_server_on_event(&server, on_tick, &counter);
  sse_server_run(&server);
  sse_server_cleanup(&server);

  return 0;
}
