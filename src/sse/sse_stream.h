#ifndef SSE_SSE_STREAM_H_
#define SSE_SSE_STREAM_H_

#include "../util/types.h"
#include "sse_conn.h"
#include "sse_event.h"

enum {
  SSE_CONN_MAX             = 64,
  SSE_EVENT_QUEUE_CAPACITY = 128
};

typedef struct {
  SSEEvent event;
  bool     active;
} SSEEventSlot;

typedef struct {
  SSEConnection connections[SSE_CONN_MAX];
  size_t        connection_count;
  SSEEventSlot  event_queue[SSE_EVENT_QUEUE_CAPACITY];
  size_t        queue_head;
  size_t        queue_size;
} SSEStream;

void           sse_stream_init(SSEStream* stream);
int32_t        sse_stream_add_connection(SSEStream* stream, const int32_t fd);
bool           sse_stream_remove_connection(SSEStream* stream, const int32_t fd);
SSEConnection* sse_stream_find_connection(SSEStream* stream, const int32_t fd);
size_t         sse_stream_conn_count(const SSEStream* stream);
void           sse_stream_enqueue_event(SSEStream* stream, const SSEEvent* event);

#endif
