#include "sse_conn.h"
#include "sse_event.h"

#include "../util/allocator.h"
#include "../util/string.h"
#include "../arch/send.h"

static inline bool header_key_matches(
  const HTTPRequestHeaderLine* header,
  const char*                  key,
  size_t                       key_len)
{
  require_not_null(header, false);
  require_not_null(key, false);

  size_t header_key_len = nostr_strnlen(header->key, HTTP_HEADER_KEY_CAPACITY);
  if (header_key_len != key_len) {
    return false;
  }

  return nostr_strncmp(header->key, key, key_len);
}

void sse_conn_init(SSEConnection* conn)
{
  require_not_null(conn, );

  conn->fd          = -1;
  conn->state       = SSE_CONN_STATE_INACTIVE;
  conn->header_sent = false;
  _memset(conn->last_event_id, 0, SSE_EVENT_ID_CAPACITY);
}

bool sse_conn_open(SSEConnection* conn, int32_t fd)
{
  require_not_null(conn, false);

  conn->fd    = fd;
  conn->state = SSE_CONN_STATE_ACTIVE;
  return true;
}

bool sse_conn_extract_last_event_id(
  SSEConnection*              conn,
  const HTTPRequestHeaderLine headers[],
  size_t                      header_size)
{
  require_not_null(conn, false);
  require_not_null(headers, false);

  const char* key     = "Last-Event-ID";
  size_t      key_len = 13;

  for (size_t i = 0; i < header_size; i++) {
    if (header_key_matches(&headers[i], key, key_len)) {
      size_t value_len = nostr_strnlen(headers[i].value, HTTP_HEADER_VALUE_CAPACITY);
      if (value_len >= SSE_EVENT_ID_CAPACITY) {
        value_len = SSE_EVENT_ID_CAPACITY - 1;
      }
      _memcpy(conn->last_event_id, headers[i].value, value_len);
      conn->last_event_id[value_len] = '\0';
      return true;
    }
  }

  return false;
}

void sse_conn_close(SSEConnection* conn)
{
  require_not_null(conn, );

  conn->fd          = -1;
  conn->state       = SSE_CONN_STATE_INACTIVE;
  conn->header_sent = false;
}

bool sse_conn_is_active(const SSEConnection* conn)
{
  require_not_null(conn, false);

  return conn->state == SSE_CONN_STATE_ACTIVE;
}

bool sse_conn_send_header(SSEConnection* conn)
{
  require_not_null(conn, false);

  char   buf[SSE_SERIALIZE_BUFFER_CAPACITY];
  size_t len = sse_build_response_header(buf, sizeof(buf));
  if (len == 0) {
    return false;
  }

  ssize_t sent = internal_sendto(conn->fd, buf, len, 0, NULL, 0);
  if (sent < 0) {
    return false;
  }

  conn->header_sent = true;
  return true;
}

bool sse_conn_send_event(SSEConnection* conn, const SSEEvent* event)
{
  require_not_null(conn, false);
  require_not_null(event, false);

  char   buf[SSE_SERIALIZE_BUFFER_CAPACITY];
  size_t len = sse_event_serialize(event, buf, sizeof(buf));
  if (len == 0) {
    return false;
  }

  ssize_t sent = internal_sendto(conn->fd, buf, len, 0, NULL, 0);
  if (sent < 0) {
    return false;
  }

  return true;
}
