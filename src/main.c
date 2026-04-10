/*
 * Sample SSE server
 *
 * Listens on port 8080, accepts connections, and sends SSE events
 * every 2 seconds to all connected clients.
 *
 * Usage:
 *   curl -N http://localhost:8080/events
 */

#include "arch/accept.h"
#include "arch/close.h"
#include "arch/epoll.h"
#include "arch/linux/epoll.h"
#include "arch/linux/fcntl.h"
#include "arch/linux/sockoption.h"
#include "arch/listen.h"
#include "arch/optimize_socket.h"
#include "arch/recv.h"
#include "arch/send.h"
#include "http/http.h"
#include "sse/sse_conn.h"
#include "sse/sse_event.h"
#include "sse/sse_stream.h"
#include "util/allocator.h"
#include "util/log.h"
#include "util/signal.h"
#include "util/string.h"
#include "util/types.h"

enum {
  SERVER_PORT       = 8080,
  SERVER_BACKLOG    = 128,
  EPOLL_MAX_EVENTS  = 64,
  RECV_BUFFER_SIZE  = 4096,
  EPOLL_TIMEOUT_MS  = 2000,
  MSG_NOSIGNAL_FLAG = 0x4000
};

static uint16_t htons_impl(uint16_t val)
{
  return (uint16_t)((val >> 8) | (val << 8));
}

static bool set_nonblocking(int32_t fd)
{
  int32_t flags = internal_fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return internal_fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

static int32_t create_listen_socket(uint16_t port)
{
  int32_t fd = internal_socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    log_error("socket() failed");
    return -1;
  }

  int32_t opt = 1;
  internal_setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  struct sockaddr_in addr;
  _memset(&addr, 0, sizeof(addr));
  addr.sin_family      = AF_INET;
  addr.sin_port        = htons_impl(port);
  addr.sin_addr.s_addr = INADDR_ANY;

  if (internal_bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
    log_error("bind() failed");
    internal_close(fd);
    return -1;
  }

  if (internal_listen(fd, SERVER_BACKLOG) < 0) {
    log_error("listen() failed");
    internal_close(fd);
    return -1;
  }

  set_nonblocking(fd);
  return fd;
}

static void handle_new_connection(
  int32_t    listen_fd,
  int32_t    epoll_fd,
  SSEStream* stream)
{
  int32_t client_fd = internal_accept(listen_fd, NULL, NULL, 0);
  if (client_fd < 0) {
    return;
  }

  set_nonblocking(client_fd);

  /* Read the HTTP request */
  char    recv_buf[RECV_BUFFER_SIZE];
  ssize_t n = internal_recvfrom(client_fd, recv_buf, sizeof(recv_buf) - 1, 0, NULL, NULL);
  if (n <= 0) {
    internal_close(client_fd);
    return;
  }
  recv_buf[n] = '\0';

  /* Parse HTTP request */
  HTTPRequest request;
  _memset(&request, 0, sizeof(request));
  if (!extract_http_request(recv_buf, (size_t)n, &request)) {
    internal_close(client_fd);
    return;
  }

  /* Check if this is an SSE request (GET method) */
  if (!sse_stream_is_sse_request(&request)) {
    /* Not an SSE request - send 404 */
    const char response_404[] =
      "HTTP/1.1 404 Not Found\r\n"
      "Content-Length: 0\r\n"
      "\r\n";
    internal_sendto(client_fd, response_404, sizeof(response_404) - 1, 0, NULL, 0);
    internal_close(client_fd);
    return;
  }

  /* Add to stream and send SSE headers */
  int32_t slot = sse_stream_add_connection(stream, client_fd);
  if (slot < 0) {
    internal_close(client_fd);
    return;
  }

  /* Extract Last-Event-ID if present */
  SSEConnection* conn = sse_stream_find_connection(stream, client_fd);
  if (conn != NULL) {
    sse_conn_extract_last_event_id(conn, request.headers, request.header_size);
  }

  /* Send SSE response header */
  char   header_buf[SSE_SERIALIZE_BUFFER_CAPACITY];
  size_t header_len = sse_build_response_header(header_buf, sizeof(header_buf));
  internal_sendto(client_fd, header_buf, header_len, 0, NULL, 0);

  /* Register with epoll for disconnect detection */
  SSEEpollEvent ev;
  ev.events  = EPOLLIN | EPOLLRDHUP;
  ev.data.fd = client_fd;
  internal_epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev);

  log_info("Client connected");
}

static void handle_client_disconnect(
  int32_t    client_fd,
  int32_t    epoll_fd,
  SSEStream* stream)
{
  internal_epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, NULL);
  sse_stream_remove_connection(stream, client_fd);
  internal_close(client_fd);
  log_info("Client disconnected");
}

int main(void)
{
  int32_t listen_fd = create_listen_socket(SERVER_PORT);
  if (listen_fd < 0) {
    return 1;
  }

  int32_t epoll_fd = internal_epoll_create1(0);
  if (epoll_fd < 0) {
    internal_close(listen_fd);
    return 1;
  }

  /* Register listen socket with epoll */
  SSEEpollEvent ev;
  ev.events  = EPOLLIN;
  ev.data.fd = listen_fd;
  internal_epoll_ctl(epoll_fd, EPOLL_CTL_ADD, listen_fd, &ev);

  static SSEStream stream;
  sse_stream_init(&stream);

  uint32_t event_counter = 0;

  log_info("SSE server listening on port 8080");

  bool running = true;
  while (running) {
    SSEEpollEvent events[EPOLL_MAX_EVENTS];
    int32_t             nfds = internal_epoll_wait(epoll_fd, events, EPOLL_MAX_EVENTS, EPOLL_TIMEOUT_MS);

    /* Handle epoll events */
    for (int32_t i = 0; i < nfds; i++) {
      if (events[i].data.fd == listen_fd) {
        handle_new_connection(listen_fd, epoll_fd, &stream);
      } else if (events[i].events & (EPOLLRDHUP | EPOLLERR | EPOLLHUP)) {
        handle_client_disconnect(events[i].data.fd, epoll_fd, &stream);
      }
    }

    /* Send periodic SSE event to all connected clients */
    if (sse_stream_conn_count(&stream) > 0) {
      event_counter++;

      SSEEvent event;
      sse_event_init(&event);

      /* Build event data: "count:<N>" */
      char count_str[16];
      _memset(count_str, 0, sizeof(count_str));
      itoa(event_counter, count_str, sizeof(count_str));

      const char* prefix     = "count:";
      size_t      prefix_len = 6;
      size_t      count_len  = sse_strnlen(count_str, sizeof(count_str));
      _memcpy(event.data, prefix, prefix_len);
      _memcpy(event.data + prefix_len, count_str, count_len);

      /* Set event type and id */
      _memcpy(event.event_type, "tick", 4);
      _memcpy(event.id, count_str, count_len);

      size_t sent = sse_stream_broadcast(&stream, &event);
      if (sent > 0) {
        log_info("Broadcast event");
      }
    }
  }

  /* Cleanup */
  for (size_t i = 0; i < SSE_CONN_MAX; i++) {
    if (sse_conn_is_active(&stream.connections[i])) {
      internal_close(stream.connections[i].fd);
    }
  }
  internal_close(epoll_fd);
  internal_close(listen_fd);
  return 0;
}
