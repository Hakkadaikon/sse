extern "C" {
#include "sse/sse.h"
}
#undef value
#include <gtest/gtest.h>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <thread>
#include <chrono>
#include <string>

class SSEServerAPITest : public ::testing::Test {
protected:
  SSEServer server;
  SSEServerConfig config;

  void SetUp() override {
    memset(&server, 0, sizeof(server));
    memset(&config, 0, sizeof(config));
    config.port = 0;  // OS assigns port
    config.backlog = 8;
    config.event_interval_ms = 100;
  }

  void TearDown() override {
    sse_server_cleanup(&server);
  }
};

// 1. Init sets default state
TEST_F(SSEServerAPITest, InitSetsDefaultState) {
  bool result = sse_server_init(&server, &config);

  EXPECT_TRUE(result);
  EXPECT_GE(server.listen_fd, 0);
  EXPECT_GE(server.epoll_fd, 0);
  EXPECT_EQ(server.on_event, nullptr);
  EXPECT_EQ(server.on_connect, nullptr);
  EXPECT_EQ(server.on_disconnect, nullptr);
  EXPECT_EQ(server.user_data, nullptr);
  EXPECT_FALSE(server.running);
}

// 2. Register event callback
TEST_F(SSEServerAPITest, OnEventRegistersCallback) {
  sse_server_init(&server, &config);

  int dummy = 42;
  auto cb = [](SSEEvent*, void*) -> bool { return true; };
  sse_server_on_event(&server, cb, &dummy);

  EXPECT_EQ(server.on_event, cb);
  EXPECT_EQ(server.user_data, &dummy);
}

// 3. Register connect callback
TEST_F(SSEServerAPITest, OnConnectRegistersCallback) {
  sse_server_init(&server, &config);

  auto cb = [](int32_t, void*) {};
  sse_server_on_connect(&server, cb);

  EXPECT_EQ(server.on_connect, cb);
}

// 4. Register disconnect callback
TEST_F(SSEServerAPITest, OnDisconnectRegistersCallback) {
  sse_server_init(&server, &config);

  auto cb = [](int32_t, void*) {};
  sse_server_on_disconnect(&server, cb);

  EXPECT_EQ(server.on_disconnect, cb);
}

// 5. Stop sets running to false
TEST_F(SSEServerAPITest, StopSetsRunningFalse) {
  sse_server_init(&server, &config);
  server.running = true;

  sse_server_stop(&server);

  EXPECT_FALSE(server.running);
}

// 6. Cleanup closes file descriptors
TEST_F(SSEServerAPITest, CleanupClosesFileDescriptors) {
  sse_server_init(&server, &config);
  int32_t listen_fd = server.listen_fd;
  int32_t epoll_fd = server.epoll_fd;

  sse_server_cleanup(&server);

  EXPECT_EQ(server.listen_fd, -1);
  EXPECT_EQ(server.epoll_fd, -1);
}

// 7. Run with event callback broadcasts to connected client
TEST_F(SSEServerAPITest, RunBroadcastsEventsToClient) {
  // Use a real port for this test
  config.port = 18090;
  config.event_interval_ms = 50;
  sse_server_init(&server, &config);

  static int call_count = 0;
  call_count = 0;

  sse_server_on_event(&server, [](SSEEvent* event, void* ud) -> bool {
    int* count = (int*)ud;
    (*count)++;
    memcpy(event->data, "hello", 5);
    if (*count >= 3) return false;  // stop after 2 broadcasts
    return true;
  }, &call_count);

  // Run server in a thread
  std::thread server_thread([this]() {
    sse_server_run(&server);
  });

  // Give server time to start
  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  // Connect a client
  int client_fd = socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(client_fd, 0);

  struct timeval tv = {2, 0};
  setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  struct sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(config.port);
  addr.sin_addr.s_addr = inet_addr("127.0.0.1");

  int conn_result = connect(client_fd, (struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(conn_result, 0);

  // Send HTTP GET request
  const char* request = "GET /events HTTP/1.1\r\nAccept: text/event-stream\r\n\r\n";
  send(client_fd, request, strlen(request), 0);

  // Read response (headers + events)
  std::string response;
  char buf[4096];
  for (int attempt = 0; attempt < 5; attempt++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
    if (n > 0) {
      buf[n] = '\0';
      response += buf;
    }
    if (response.find("data:hello") != std::string::npos) break;
  }

  EXPECT_FALSE(response.empty());
  EXPECT_NE(response.find("text/event-stream"), std::string::npos);
  EXPECT_NE(response.find("data:hello"), std::string::npos);

  // Stop server
  close(client_fd);
  sse_server_stop(&server);
  server_thread.join();
}
