#ifndef SSE_SSE_CONN_H_
#define SSE_SSE_CONN_H_

#include "../util/types.h"
#include "../http/http.h"
#include "sse_event.h"

enum {
  SSE_CONN_STATE_INACTIVE  = 0,
  SSE_CONN_STATE_ACTIVE    = 1
};

typedef struct {
  int32_t fd;
  int32_t state;
  char    last_event_id[SSE_EVENT_ID_CAPACITY];
  bool    header_sent;
} SSEConnection;

void sse_conn_init(SSEConnection* conn);
bool sse_conn_open(SSEConnection* conn, int32_t fd);
bool sse_conn_extract_last_event_id(
  SSEConnection*              conn,
  const HTTPRequestHeaderLine headers[],
  size_t                      header_size);
void sse_conn_close(SSEConnection* conn);
bool sse_conn_is_active(const SSEConnection* conn);
bool sse_conn_send_header(SSEConnection* conn);
bool sse_conn_send_event(SSEConnection* conn, const SSEEvent* event);

#endif
