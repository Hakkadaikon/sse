#ifndef SSE_SSE_SSE_H_
#define SSE_SSE_SSE_H_

#include "sse_conn.h"
#include "sse_event.h"
#include "sse_stream.h"

typedef bool (*SSEEventCallback)(SSEEvent* event, void* user_data);
typedef void (*SSEConnectCallback)(int32_t client_fd, void* user_data);
typedef void (*SSEDisconnectCallback)(int32_t client_fd, void* user_data);

typedef struct {
  uint16_t port;
  int32_t  backlog;
  int32_t  event_interval_ms;
} SSEServerConfig;

typedef struct {
  SSEServerConfig       config;
  SSEStream             stream;
  int32_t               listen_fd;
  int32_t               epoll_fd;
  SSEEventCallback      on_event;
  SSEConnectCallback    on_connect;
  SSEDisconnectCallback on_disconnect;
  void*                 user_data;
  bool                  running;
} SSEServer;

bool sse_server_init(SSEServer* server, const SSEServerConfig* config);
void sse_server_on_event(SSEServer* server, SSEEventCallback cb, void* user_data);
void sse_server_on_connect(SSEServer* server, SSEConnectCallback cb);
void sse_server_on_disconnect(SSEServer* server, SSEDisconnectCallback cb);
bool sse_server_run(SSEServer* server);
void sse_server_stop(SSEServer* server);
void sse_server_cleanup(SSEServer* server);

#endif
