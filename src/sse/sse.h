/**
 * @file sse.h
 * @brief High-level Server-Sent Events (SSE) server API.
 *
 * Provides a callback-based interface for building SSE servers.
 * Users only need to include this single header — it pulls in all
 * necessary SSE subsystem headers (event, connection, stream).
 *
 * Typical usage:
 * @code
 *   SSEServer server;
 *   SSEServerConfig config = { .port = 8080, .backlog = 128, .event_interval_ms = 2000 };
 *
 *   sse_server_init(&server, &config);
 *   sse_server_on_event(&server, my_callback, my_user_data);
 *   sse_server_run(&server);      // blocks until SIGINT/SIGTERM
 *   sse_server_cleanup(&server);
 * @endcode
 */

#ifndef SSE_SSE_SSE_H_
#define SSE_SSE_SSE_H_

#include "sse_conn.h"
#include "sse_event.h"
#include "sse_stream.h"

/**
 * @brief Event callback invoked periodically while clients are connected.
 *
 * The server calls this function on each iteration of the event loop
 * (controlled by @ref SSEServerConfig::event_interval_ms) when at least
 * one client is connected. The callback should populate the @p event
 * fields (data, event_type, id, retry_ms) and return @c true to
 * broadcast the event to all connected clients, or @c false to skip.
 *
 * @param event     Pre-initialized SSEEvent to fill in. All fields are
 *                  zeroed and retry_ms is set to SSE_RETRY_UNSET before
 *                  each call.
 * @param user_data Opaque pointer registered via sse_server_on_event().
 * @return true     Broadcast the event to all connected clients.
 * @return false    Skip this cycle without sending anything.
 */
typedef bool (*SSEEventCallback)(SSEEvent* event, void* user_data);

/**
 * @brief Connect callback invoked when a new SSE client connects.
 *
 * Called after the server has accepted the TCP connection, parsed the
 * HTTP request, verified it is a GET request, sent the SSE response
 * headers, and registered the client in the connection pool.
 *
 * @param client_fd File descriptor of the newly connected client socket.
 * @param user_data Opaque pointer registered via sse_server_on_event().
 */
typedef void (*SSEConnectCallback)(int32_t client_fd, void* user_data);

/**
 * @brief Disconnect callback invoked when a client disconnects.
 *
 * Called before the server removes the client from the connection pool
 * and closes the file descriptor. Triggered by EPOLLRDHUP, EPOLLERR,
 * or EPOLLHUP events.
 *
 * @param client_fd File descriptor of the disconnecting client socket.
 * @param user_data Opaque pointer registered via sse_server_on_event().
 */
typedef void (*SSEDisconnectCallback)(int32_t client_fd, void* user_data);

/**
 * @brief Server configuration parameters.
 *
 * Passed to sse_server_init() to configure the listening socket and
 * event loop behavior.
 */
typedef struct {
  /** @brief TCP port number to listen on (e.g., 8080). */
  uint16_t port;

  /**
   * @brief Maximum pending connections for listen().
   *
   * Set to 0 to use the default value of 128.
   */
  int32_t backlog;

  /**
   * @brief Event loop interval in milliseconds.
   *
   * Controls the epoll_wait timeout. After each timeout (or epoll event
   * batch), the server invokes the event callback if clients are
   * connected. A value of 2000 means the callback fires roughly every
   * 2 seconds when idle.
   */
  int32_t event_interval_ms;
} SSEServerConfig;

/**
 * @brief SSE server instance.
 *
 * Holds all state required to run the server: configuration, the
 * connection/stream manager, file descriptors, callbacks, and the
 * event loop running flag. Allocate on the stack or as a static
 * variable — the struct is large due to the embedded SSEStream
 * (contains the connection pool and event queue).
 *
 * @note The SSEStream member is ~600 KB. Use a static or global
 *       variable rather than a stack-local to avoid stack overflow.
 */
typedef struct {
  /** @brief Copy of the configuration passed to sse_server_init(). */
  SSEServerConfig config;

  /**
   * @brief Stream manager holding connection pool and event queue.
   *
   * Manages up to SSE_CONN_MAX concurrent connections and a ring
   * buffer of SSE_EVENT_QUEUE_CAPACITY recent events for Last-Event-ID
   * replay.
   */
  SSEStream stream;

  /**
   * @brief Listening socket file descriptor.
   *
   * Set to -1 before initialization and after cleanup.
   */
  int32_t listen_fd;

  /**
   * @brief epoll instance file descriptor.
   *
   * Used to multiplex the listening socket and all client connections.
   * Set to -1 before initialization and after cleanup.
   */
  int32_t epoll_fd;

  /**
   * @brief Event callback, or NULL if not registered.
   * @see SSEEventCallback
   */
  SSEEventCallback on_event;

  /**
   * @brief Connect callback, or NULL if not registered.
   * @see SSEConnectCallback
   */
  SSEConnectCallback on_connect;

  /**
   * @brief Disconnect callback, or NULL if not registered.
   * @see SSEDisconnectCallback
   */
  SSEDisconnectCallback on_disconnect;

  /**
   * @brief Opaque user data pointer passed to all callbacks.
   *
   * Allows the application to pass arbitrary state (counters, database
   * handles, configuration, etc.) through to callback functions without
   * using global variables.
   */
  void* user_data;

  /**
   * @brief Event loop running flag.
   *
   * Set to true by sse_server_run() and cleared by sse_server_stop()
   * or on receipt of SIGINT/SIGTERM.
   */
  bool running;
} SSEServer;

/**
 * @brief Initialize the SSE server.
 *
 * Creates a TCP socket, binds to the configured port, starts listening,
 * sets up an epoll instance, initializes the connection/stream manager,
 * and registers signal handlers for SIGHUP, SIGINT, and SIGTERM.
 *
 * @param server Pointer to an uninitialized SSEServer instance.
 * @param config Pointer to the server configuration. Copied internally.
 * @return true  Initialization succeeded; the server is ready to run.
 * @return false Initialization failed (socket/bind/listen/epoll error).
 */
bool sse_server_init(SSEServer* server, const SSEServerConfig* config);

/**
 * @brief Register the periodic event callback.
 *
 * The callback is invoked on each event loop iteration when at least
 * one client is connected. Only one event callback can be registered;
 * calling this function again replaces the previous callback.
 *
 * @param server    Initialized SSEServer instance.
 * @param cb        Event callback function, or NULL to unregister.
 * @param user_data Opaque pointer forwarded to all callbacks
 *                  (event, connect, disconnect).
 */
void sse_server_on_event(SSEServer* server, SSEEventCallback cb, void* user_data);

/**
 * @brief Register an optional client connect callback.
 *
 * Called after a new SSE client has been fully set up (HTTP handshake
 * complete, SSE headers sent, registered in the connection pool).
 *
 * @param server Initialized SSEServer instance.
 * @param cb     Connect callback function, or NULL to unregister.
 */
void sse_server_on_connect(SSEServer* server, SSEConnectCallback cb);

/**
 * @brief Register an optional client disconnect callback.
 *
 * Called when a client disconnects (detected via epoll RDHUP/ERR/HUP),
 * before the file descriptor is closed and the connection is removed
 * from the pool.
 *
 * @param server Initialized SSEServer instance.
 * @param cb     Disconnect callback function, or NULL to unregister.
 */
void sse_server_on_disconnect(SSEServer* server, SSEDisconnectCallback cb);

/**
 * @brief Run the server event loop (blocking).
 *
 * Enters a blocking loop that:
 *  1. Waits for epoll events (new connections, client disconnects)
 *  2. Accepts new SSE clients (validates GET method, sends headers)
 *  3. Removes disconnected clients
 *  4. Invokes the event callback and broadcasts the resulting event
 *
 * The loop continues until sse_server_stop() is called or a signal
 * (SIGINT, SIGTERM, SIGHUP) is received. On exit, sse_server_cleanup()
 * is called automatically.
 *
 * @param server Initialized SSEServer instance with callbacks registered.
 * @return true  Clean shutdown.
 * @return false Server was not initialized or a fatal error occurred.
 */
bool sse_server_run(SSEServer* server);

/**
 * @brief Signal the event loop to stop.
 *
 * Sets the running flag to false, causing sse_server_run() to exit on
 * the next iteration. Safe to call from any thread or signal handler.
 *
 * @param server Running SSEServer instance.
 */
void sse_server_stop(SSEServer* server);

/**
 * @brief Clean up all server resources.
 *
 * Closes all active client connections, the epoll instance, and the
 * listening socket. Sets listen_fd and epoll_fd to -1. Safe to call
 * multiple times (subsequent calls are no-ops).
 *
 * @note This is called automatically by sse_server_run() on exit.
 *       Calling it again from main() is safe but redundant.
 *
 * @param server SSEServer instance to clean up.
 */
void sse_server_cleanup(SSEServer* server);

#endif
