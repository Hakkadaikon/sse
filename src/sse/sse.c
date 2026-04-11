#include "sse.h"

#include "../arch/accept.h"
#include "../arch/close.h"
#include "../arch/epoll.h"
#include "../arch/linux/epoll.h"
#include "../arch/linux/fcntl.h"
#include "../arch/linux/sockoption.h"
#include "../arch/listen.h"
#include "../arch/optimize_socket.h"
#include "../arch/recv.h"
#include "../arch/send.h"
#include "../http/http.h"
#include "../util/allocator.h"
#include "../util/log.h"
#include "../util/signal.h"
#include "../util/string.h"

enum {
  SSE_RECV_BUFFER_SIZE = 4096,
  SSE_EPOLL_MAX_EVENTS = 64
};

static inline uint16_t sse_htons(uint16_t val)
{
  return (uint16_t)((val >> 8) | (val << 8));
}

static inline bool sse_set_nonblocking(int32_t fd)
{
  int32_t flags = internal_fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return internal_fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

static inline void handle_new_connection(SSEServer* server)
{
  int32_t client_fd = internal_accept(server->listen_fd, NULL, NULL, 0);
  if (client_fd < 0) {
    return;
  }

  sse_set_nonblocking(client_fd);

  char    recv_buf[SSE_RECV_BUFFER_SIZE];
  ssize_t n = internal_recvfrom(client_fd, recv_buf, sizeof(recv_buf) - 1, 0, NULL, NULL);
  if (n <= 0) {
    internal_close(client_fd);
    return;
  }
  recv_buf[n] = '\0';

  HTTPRequest request;
  _memset(&request, 0, sizeof(request));
  if (!extract_http_request(recv_buf, (size_t)n, &request)) {
    internal_close(client_fd);
    return;
  }

  if (!sse_stream_is_sse_request(&request)) {
    const char response_404[] =
      "HTTP/1.1 404 Not Found\r\n"
      "Content-Length: 0\r\n"
      "\r\n";
    internal_sendto(client_fd, response_404, sizeof(response_404) - 1, 0, NULL, 0);
    internal_close(client_fd);
    return;
  }

  int32_t slot = sse_stream_add_connection(&server->stream, client_fd);
  if (slot < 0) {
    internal_close(client_fd);
    return;
  }

  SSEConnection* conn = sse_stream_find_connection(&server->stream, client_fd);
  if (conn != NULL) {
    sse_conn_extract_last_event_id(conn, request.headers, request.header_size);
  }

  char   header_buf[SSE_SERIALIZE_BUFFER_CAPACITY];
  size_t header_len = sse_build_response_header(header_buf, sizeof(header_buf));
  internal_sendto(client_fd, header_buf, header_len, 0, NULL, 0);

  SSEEpollEvent ev;
  ev.events  = EPOLLIN | EPOLLRDHUP;
  ev.data.fd = client_fd;
  internal_epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);

  if (server->on_connect != NULL) {
    server->on_connect(client_fd, server->user_data);
  }
}

static inline void handle_client_disconnect(SSEServer* server, int32_t client_fd)
{
  if (server->on_disconnect != NULL) {
    server->on_disconnect(client_fd, server->user_data);
  }

  internal_epoll_ctl(server->epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
  sse_stream_remove_connection(&server->stream, client_fd);
  internal_close(client_fd);
}

bool sse_server_init(SSEServer* server, const SSEServerConfig* config)
{
  require_not_null(server, false);
  require_not_null(config, false);

  _memset(server, 0, sizeof(SSEServer));
  _memcpy(&server->config, config, sizeof(SSEServerConfig));

  server->listen_fd     = -1;
  server->epoll_fd      = -1;
  server->on_event      = NULL;
  server->on_connect    = NULL;
  server->on_disconnect = NULL;
  server->user_data     = NULL;
  server->running       = false;

  int32_t fd = internal_socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return false;
  }

  int32_t opt = 1;
  internal_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  _memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_port        = sse_htons(config->port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (internal_bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    internal_close(fd);
    return false;
  }

  int32_t backlog = config->backlog > 0 ? config->backlog : 128;
  if (internal_listen(fd, backlog) < 0) {
    internal_close(fd);
    return false;
  }

  sse_set_nonblocking(fd);
  server->listen_fd = fd;

  int32_t epoll_fd = internal_epoll_create1(0);
  if (epoll_fd < 0) {
    internal_close(fd);
    server->listen_fd = -1;
    return false;
  }
  server->epoll_fd = epoll_fd;

  sse_stream_init(&server->stream);

  _signal_init();

  return true;
}

void sse_server_on_event(SSEServer* server, SSEEventCallback cb, void* user_data)
{
  require_not_null(server, );

  server->on_event  = cb;
  server->user_data = user_data;
}

void sse_server_on_connect(SSEServer* server, SSEConnectCallback cb)
{
  require_not_null(server, );

  server->on_connect = cb;
}

void sse_server_on_disconnect(SSEServer* server, SSEDisconnectCallback cb)
{
  require_not_null(server, );

  server->on_disconnect = cb;
}

bool sse_server_run(SSEServer* server)
{
  require_not_null(server, false);

  SSEEpollEvent ev;
  ev.events  = EPOLLIN;
  ev.data.fd = server->listen_fd;
  internal_epoll_ctl(server->epoll_fd, EPOLL_CTL_ADD, server->listen_fd, &ev);

  server->running = true;

  while (server->running && !_is_rise_signal()) {
    SSEEpollEvent events[SSE_EPOLL_MAX_EVENTS];
    int32_t       nfds = internal_epoll_wait(
      server->epoll_fd, events, SSE_EPOLL_MAX_EVENTS, server->config.event_interval_ms);

    if (_is_rise_signal()) {
      break;
    }

    for (int32_t i = 0; i < nfds; i++) {
      if (events[i].data.fd == server->listen_fd) {
        handle_new_connection(server);
      } else if (events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
        handle_client_disconnect(server, events[i].data.fd);
      }
    }

    if (server->on_event != NULL && sse_stream_conn_count(&server->stream) > 0) {
      SSEEvent event;
      sse_event_init(&event);

      if (server->on_event(&event, server->user_data)) {
        sse_stream_broadcast(&server->stream, &event);
      }
    }
  }

  sse_server_cleanup(server);
  return true;
}

void sse_server_stop(SSEServer* server)
{
  require_not_null(server, );

  server->running = false;
}

void sse_server_cleanup(SSEServer* server)
{
  require_not_null(server, );

  for (size_t i = 0; i < SSE_CONN_MAX; i++) {
    if (sse_conn_is_active(&server->stream.connections[i])) {
      internal_close(server->stream.connections[i].fd);
    }
  }

  if (server->epoll_fd >= 0) {
    internal_close(server->epoll_fd);
    server->epoll_fd = -1;
  }

  if (server->listen_fd >= 0) {
    internal_close(server->listen_fd);
    server->listen_fd = -1;
  }
}
