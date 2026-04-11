#include "sse_event.h"

#include "../util/allocator.h"
#include "../util/string.h"

static inline bool append_to_buffer(
  char*        buf,
  const size_t capacity,
  size_t*      pos,
  const char*  src,
  const size_t src_len)
{
  if (*pos + src_len >= capacity) {
    return false;
  }
  _memcpy(buf + *pos, src, src_len);
  *pos += src_len;
  return true;
}

static inline size_t serialize_field_data(
  const char*  data,
  char*        buffer,
  const size_t buffer_capacity,
  size_t*      pos)
{
  require_not_null(data, 0);

  const char* src = data;
  while (*src != '\0') {
    if (!append_to_buffer(buffer, buffer_capacity, pos, "data:", 5)) {
      return 0;
    }
    while (*src != '\0' && *src != '\n') {
      if (*pos >= buffer_capacity) return 0;
      buffer[(*pos)++] = *src++;
    }
    if (*pos >= buffer_capacity) return 0;
    buffer[(*pos)++] = '\n';
    if (*src == '\n') src++;
  }
  return 1;
}

static inline size_t serialize_field_event(
  const char*  event_type,
  char*        buffer,
  const size_t buffer_capacity,
  size_t*      pos)
{
  if (event_type[0] == '\0') return 1;

  size_t len = _strnlen(event_type, SSE_EVENT_TYPE_CAPACITY);
  if (!append_to_buffer(buffer, buffer_capacity, pos, "event:", 6)) return 0;
  if (!append_to_buffer(buffer, buffer_capacity, pos, event_type, len)) return 0;
  if (*pos >= buffer_capacity) return 0;
  buffer[(*pos)++] = '\n';
  return 1;
}

static inline size_t serialize_field_id(
  const char*  id,
  char*        buffer,
  const size_t buffer_capacity,
  size_t*      pos)
{
  if (id[0] == '\0') return 1;

  size_t len = _strnlen(id, SSE_EVENT_ID_CAPACITY);
  if (!append_to_buffer(buffer, buffer_capacity, pos, "id:", 3)) return 0;
  if (!append_to_buffer(buffer, buffer_capacity, pos, id, len)) return 0;
  if (*pos >= buffer_capacity) return 0;
  buffer[(*pos)++] = '\n';
  return 1;
}

static inline size_t serialize_field_retry(
  const int32_t retry_ms,
  char*         buffer,
  const size_t  buffer_capacity,
  size_t*       pos)
{
  if (retry_ms == SSE_RETRY_UNSET) return 1;

  char num_buf[16];
  _memset(num_buf, 0, sizeof(num_buf));
  size_t num_len = _itoa(retry_ms, num_buf, sizeof(num_buf));
  if (num_len == 0) return 0;

  if (!append_to_buffer(buffer, buffer_capacity, pos, "retry:", 6)) return 0;
  if (!append_to_buffer(buffer, buffer_capacity, pos, num_buf, num_len)) return 0;
  if (*pos >= buffer_capacity) return 0;
  buffer[(*pos)++] = '\n';
  return 1;
}

bool sse_event_init(SSEEvent* event)
{
  require_not_null(event, false);

  _memset(event, 0, sizeof(SSEEvent));
  event->retry_ms = SSE_RETRY_UNSET;
  return true;
}

size_t sse_event_serialize(
  const SSEEvent* event,
  char*           buffer,
  const size_t    buffer_capacity)
{
  require_not_null(event, 0);
  require_not_null(buffer, 0);
  require_valid_length(buffer_capacity, 0);

  if (event->data[0] == '\0') {
    return 0;
  }

  size_t pos = 0;

  if (!serialize_field_id(event->id, buffer, buffer_capacity, &pos)) return 0;
  if (!serialize_field_event(event->event_type, buffer, buffer_capacity, &pos)) return 0;
  if (!serialize_field_retry(event->retry_ms, buffer, buffer_capacity, &pos)) return 0;
  if (!serialize_field_data(event->data, buffer, buffer_capacity, &pos)) return 0;

  if (pos >= buffer_capacity) return 0;
  buffer[pos++] = '\n';

  if (pos < buffer_capacity) {
    buffer[pos] = '\0';
  }

  return pos;
}

size_t sse_serialize_comment(
  const char*  comment,
  const size_t comment_length,
  char*        buffer,
  const size_t buffer_capacity)
{
  require_not_null(comment, 0);
  require_not_null(buffer, 0);
  require_valid_length(buffer_capacity, 0);

  size_t pos = 0;

  if (!append_to_buffer(buffer, buffer_capacity, &pos, ": ", 2)) return 0;
  if (comment_length > 0) {
    if (!append_to_buffer(buffer, buffer_capacity, &pos, comment, comment_length)) return 0;
  }
  if (pos >= buffer_capacity) return 0;
  buffer[pos++] = '\n';

  if (pos < buffer_capacity) {
    buffer[pos] = '\0';
  }

  return pos;
}

size_t sse_build_response_header(char* buffer, const size_t buffer_size)
{
  require_not_null(buffer, 0);
  require_valid_length(buffer_size, 0);

  const char header[] =
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/event-stream\r\n"
    "Cache-Control: no-cache\r\n"
    "Connection: keep-alive\r\n"
    "\r\n";

  size_t header_len = _strlen(header);
  if (header_len >= buffer_size) return 0;

  _memcpy(buffer, header, header_len);
  buffer[header_len] = '\0';

  return header_len;
}
