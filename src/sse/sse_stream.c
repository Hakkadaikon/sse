#include "sse_stream.h"

#include "../arch/send.h"
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

bool sse_stream_is_sse_request(const HTTPRequest* request)
{
  require_not_null(request, false);

  return sse_strncmp(request->line.method, "GET", 3);
}

size_t sse_stream_broadcast(SSEStream* stream, const SSEEvent* event)
{
  require_not_null(stream, 0);
  require_not_null(event, 0);

  char   buf[SSE_SERIALIZE_BUFFER_CAPACITY];
  size_t len = sse_event_serialize(event, buf, sizeof(buf));
  if (len == 0) {
    return 0;
  }

  sse_stream_enqueue_event(stream, event);

  size_t sent_count = 0;
  for (size_t i = 0; i < SSE_CONN_MAX; i++) {
    if (sse_conn_is_active(&stream->connections[i])) {
      ssize_t sent = internal_sendto(stream->connections[i].fd, buf, len, 0, NULL, 0);
      if (sent > 0) {
        sent_count++;
      }
    }
  }

  return sent_count;
}

size_t sse_stream_broadcast_comment(SSEStream* stream, const char* comment)
{
  require_not_null(stream, 0);
  require_not_null(comment, 0);

  size_t comment_len = sse_strnlen(comment, 256);

  char   buf[SSE_SERIALIZE_BUFFER_CAPACITY];
  size_t len = sse_serialize_comment(comment, comment_len, buf, sizeof(buf));
  if (len == 0) {
    return 0;
  }

  size_t sent_count = 0;
  for (size_t i = 0; i < SSE_CONN_MAX; i++) {
    if (sse_conn_is_active(&stream->connections[i])) {
      ssize_t sent = internal_sendto(stream->connections[i].fd, buf, len, 0, NULL, 0);
      if (sent > 0) {
        sent_count++;
      }
    }
  }

  return sent_count;
}

size_t sse_stream_replay_events(SSEStream* stream, int32_t fd, const char* last_event_id)
{
  require_not_null(stream, 0);
  require_not_null(last_event_id, 0);

  size_t replayed = 0;
  bool   found_id = false;
  size_t id_len   = sse_strnlen(last_event_id, SSE_EVENT_ID_CAPACITY);

  for (size_t i = 0; i < stream->queue_size; i++) {
    size_t index = (stream->queue_head + i) % SSE_EVENT_QUEUE_CAPACITY;

    if (!stream->event_queue[index].active) {
      continue;
    }

    if (!found_id) {
      if (sse_strncmp(stream->event_queue[index].event.id, last_event_id, id_len)) {
        found_id = true;
      }
      continue;
    }

    char   buf[SSE_SERIALIZE_BUFFER_CAPACITY];
    size_t len = sse_event_serialize(&stream->event_queue[index].event, buf, sizeof(buf));
    if (len > 0) {
      ssize_t sent = internal_sendto(fd, buf, len, 0, NULL, 0);
      if (sent > 0) {
        replayed++;
      }
    }
  }

  return replayed;
}
