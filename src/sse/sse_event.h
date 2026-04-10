#ifndef SSE_SSE_EVENT_H_
#define SSE_SSE_EVENT_H_

#include "../util/types.h"

enum {
  SSE_EVENT_DATA_CAPACITY       = 4096,
  SSE_EVENT_TYPE_CAPACITY       = 64,
  SSE_EVENT_ID_CAPACITY         = 64,
  SSE_SERIALIZE_BUFFER_CAPACITY = 8192,
  SSE_RETRY_UNSET               = -1
};

typedef struct {
  char    data[SSE_EVENT_DATA_CAPACITY];
  char    event_type[SSE_EVENT_TYPE_CAPACITY];
  char    id[SSE_EVENT_ID_CAPACITY];
  int32_t retry_ms;
} SSEEvent;

bool   sse_event_init(SSEEvent* event);
size_t sse_event_serialize(const SSEEvent* event, char* buffer, const size_t buffer_capacity);
size_t sse_serialize_comment(const char* comment, const size_t comment_length, char* buffer, const size_t buffer_capacity);
size_t sse_build_response_header(char* buffer, const size_t buffer_size);

#endif
