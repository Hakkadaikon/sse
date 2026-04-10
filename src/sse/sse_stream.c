#include "sse_stream.h"

#include "../util/allocator.h"
#include "../util/string.h"

static inline int32_t find_inactive_slot(const SSEStream* stream)
{
  for (size_t i = 0; i < SSE_CONN_MAX; i++) {
    if (!sse_conn_is_active(&stream->connections[i])) {
      return (int32_t)i;
    }
  }
  return -1;
}

static inline int32_t find_conn_by_fd(const SSEStream* stream, int32_t fd)
{
  for (size_t i = 0; i < SSE_CONN_MAX; i++) {
    if (sse_conn_is_active(&stream->connections[i]) &&
        stream->connections[i].fd == fd) {
      return (int32_t)i;
    }
  }
  return -1;
}

void sse_stream_init(SSEStream* stream)
{
  require_not_null(stream, );

  _memset(stream, 0, sizeof(SSEStream));

  for (size_t i = 0; i < SSE_CONN_MAX; i++) {
    sse_conn_init(&stream->connections[i]);
  }
}

int32_t sse_stream_add_connection(SSEStream* stream, const int32_t fd)
{
  require_not_null(stream, -1);

  int32_t slot = find_inactive_slot(stream);
  if (slot < 0) {
    return -1;
  }

  sse_conn_init(&stream->connections[slot]);
  sse_conn_open(&stream->connections[slot], fd);
  stream->connection_count++;

  return slot;
}

bool sse_stream_remove_connection(SSEStream* stream, const int32_t fd)
{
  require_not_null(stream, false);

  int32_t slot = find_conn_by_fd(stream, fd);
  if (slot < 0) {
    return false;
  }

  sse_conn_close(&stream->connections[slot]);
  stream->connection_count--;

  return true;
}

SSEConnection* sse_stream_find_connection(SSEStream* stream, const int32_t fd)
{
  require_not_null(stream, NULL);

  int32_t slot = find_conn_by_fd(stream, fd);
  if (slot < 0) {
    return NULL;
  }

  return &stream->connections[slot];
}

size_t sse_stream_conn_count(const SSEStream* stream)
{
  require_not_null(stream, 0);

  return stream->connection_count;
}

void sse_stream_enqueue_event(SSEStream* stream, const SSEEvent* event)
{
  require_not_null(stream, );
  require_not_null(event, );

  size_t index = (stream->queue_head + stream->queue_size) % SSE_EVENT_QUEUE_CAPACITY;

  _memcpy(&stream->event_queue[index].event, event, sizeof(SSEEvent));
  stream->event_queue[index].active = true;

  if (stream->queue_size < SSE_EVENT_QUEUE_CAPACITY) {
    stream->queue_size++;
  } else {
    stream->queue_head = (stream->queue_head + 1) % SSE_EVENT_QUEUE_CAPACITY;
  }
}
