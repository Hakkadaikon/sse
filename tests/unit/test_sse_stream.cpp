extern "C" {
#include "sse/sse_stream.h"
#include "http/http.h"
}
#undef value
#include <gtest/gtest.h>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

class SSEStreamTest : public ::testing::Test {
protected:
  SSEStream stream;

  void SetUp() override {
    sse_stream_init(&stream);
  }
};

// 1. InitSetsDefaultValues - 初期化後のデフォルト値
TEST_F(SSEStreamTest, InitSetsDefaultValues) {
  EXPECT_EQ(stream.connection_count, 0u);
  EXPECT_EQ(stream.queue_head, 0u);
  EXPECT_EQ(stream.queue_size, 0u);

  for (size_t i = 0; i < SSE_EVENT_QUEUE_CAPACITY; ++i) {
    EXPECT_FALSE(stream.event_queue[i].active);
  }
}

// 2. AddConnection_ReturnsIndex - 接続追加でインデックス返却
TEST_F(SSEStreamTest, AddConnection_ReturnsIndex) {
  const int32_t fd = 42;

  int32_t index = sse_stream_add_connection(&stream, fd);

  EXPECT_GE(index, 0);
  EXPECT_EQ(sse_stream_conn_count(&stream), 1u);
}

// 3. AddConnection_WhenFull_ReturnsNegative - 満杯時に-1
TEST_F(SSEStreamTest, AddConnection_WhenFull_ReturnsNegative) {
  for (int32_t i = 0; i < SSE_CONN_MAX; ++i) {
    int32_t result = sse_stream_add_connection(&stream, 100 + i);
    ASSERT_GE(result, 0) << "Failed to add connection at index " << i;
  }

  int32_t overflow_result = sse_stream_add_connection(&stream, 999);

  EXPECT_LT(overflow_result, 0);
}

// 4. RemoveConnection_Success - 接続削除
TEST_F(SSEStreamTest, RemoveConnection_Success) {
  const int32_t fd = 7;
  sse_stream_add_connection(&stream, fd);
  ASSERT_EQ(sse_stream_conn_count(&stream), 1u);

  bool removed = sse_stream_remove_connection(&stream, fd);

  EXPECT_TRUE(removed);
  EXPECT_EQ(sse_stream_conn_count(&stream), 0u);
}

// 5. RemoveConnection_NotFound - 存在しないfd
TEST_F(SSEStreamTest, RemoveConnection_NotFound) {
  sse_stream_add_connection(&stream, 10);

  bool removed = sse_stream_remove_connection(&stream, 999);

  EXPECT_FALSE(removed);
  EXPECT_EQ(sse_stream_conn_count(&stream), 1u);
}

// 6. FindConnection_Found - fd検索成功
TEST_F(SSEStreamTest, FindConnection_Found) {
  const int32_t fd = 55;
  sse_stream_add_connection(&stream, fd);

  SSEConnection* conn = sse_stream_find_connection(&stream, fd);

  EXPECT_NE(conn, nullptr);
}

// 7. FindConnection_NotFound - fd検索失敗
TEST_F(SSEStreamTest, FindConnection_NotFound) {
  sse_stream_add_connection(&stream, 10);

  SSEConnection* conn = sse_stream_find_connection(&stream, 999);

  EXPECT_EQ(conn, nullptr);
}

// 8. ConnCount_ReturnsActive - アクティブ接続数
TEST_F(SSEStreamTest, ConnCount_ReturnsActive) {
  EXPECT_EQ(sse_stream_conn_count(&stream), 0u);

  sse_stream_add_connection(&stream, 1);
  EXPECT_EQ(sse_stream_conn_count(&stream), 1u);

  sse_stream_add_connection(&stream, 2);
  EXPECT_EQ(sse_stream_conn_count(&stream), 2u);

  sse_stream_add_connection(&stream, 3);
  EXPECT_EQ(sse_stream_conn_count(&stream), 3u);

  sse_stream_remove_connection(&stream, 2);
  EXPECT_EQ(sse_stream_conn_count(&stream), 2u);
}

// 9. EnqueueEvent - イベントキュー追加
TEST_F(SSEStreamTest, EnqueueEvent) {
  SSEEvent event;
  memset(&event, 0, sizeof(event));

  sse_stream_enqueue_event(&stream, &event);

  EXPECT_EQ(stream.queue_size, 1u);
  EXPECT_TRUE(stream.event_queue[0].active);
}

// --- New feature tests (TDD: RED phase) ---

// Helper: create a socketpair and return server/client fds
static void create_socketpair(int& server_fd, int& client_fd) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);
  server_fd = fds[0];
  client_fd = fds[1];
}

// 10. IsSSERequest_GetMethod - GET method returns true
TEST_F(SSEStreamTest, IsSSERequest_GetMethod) {
  HTTPRequest request;
  memset(&request, 0, sizeof(request));
  strncpy(request.line.method, "GET", sizeof(request.line.method) - 1);

  bool result = sse_stream_is_sse_request(&request);

  EXPECT_TRUE(result);
}

// 11. IsSSERequest_PostMethod - POST method returns false
TEST_F(SSEStreamTest, IsSSERequest_PostMethod) {
  HTTPRequest request;
  memset(&request, 0, sizeof(request));
  strncpy(request.line.method, "POST", sizeof(request.line.method) - 1);

  bool result = sse_stream_is_sse_request(&request);

  EXPECT_FALSE(result);
}

// 12. Broadcast_SendsToAllActive - broadcast sends event to all active connections
TEST_F(SSEStreamTest, Broadcast_SendsToAllActive) {
  int server_fd1, client_fd1;
  int server_fd2, client_fd2;
  create_socketpair(server_fd1, client_fd1);
  create_socketpair(server_fd2, client_fd2);

  sse_stream_add_connection(&stream, server_fd1);
  sse_stream_add_connection(&stream, server_fd2);

  SSEEvent event;
  sse_event_init(&event);
  strncpy(event.data, "hello", sizeof(event.data) - 1);
  strncpy(event.id, "1", sizeof(event.id) - 1);

  size_t sent_count = sse_stream_broadcast(&stream, &event);

  EXPECT_EQ(sent_count, 2u);

  // Verify both clients received data
  char buf1[SSE_SERIALIZE_BUFFER_CAPACITY] = {0};
  char buf2[SSE_SERIALIZE_BUFFER_CAPACITY] = {0};
  ssize_t n1 = recv(client_fd1, buf1, sizeof(buf1) - 1, 0);
  ssize_t n2 = recv(client_fd2, buf2, sizeof(buf2) - 1, 0);

  EXPECT_GT(n1, 0);
  EXPECT_GT(n2, 0);

  // Both should contain the event data
  EXPECT_NE(strstr(buf1, "hello"), nullptr);
  EXPECT_NE(strstr(buf2, "hello"), nullptr);

  close(server_fd1);
  close(client_fd1);
  close(server_fd2);
  close(client_fd2);
}

// 13. BroadcastComment_SendsKeepAlive - comment broadcast sends to all connections
TEST_F(SSEStreamTest, BroadcastComment_SendsKeepAlive) {
  int server_fd, client_fd;
  create_socketpair(server_fd, client_fd);

  sse_stream_add_connection(&stream, server_fd);

  size_t sent_count = sse_stream_broadcast_comment(&stream, "keepalive");

  EXPECT_EQ(sent_count, 1u);

  char buf[1024] = {0};
  ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);

  EXPECT_GT(n, 0);
  EXPECT_NE(strstr(buf, ": keepalive\n"), nullptr);

  close(server_fd);
  close(client_fd);
}

// 14. ReplayEvents_AfterLastEventId - replays only events after last_event_id
TEST_F(SSEStreamTest, ReplayEvents_AfterLastEventId) {
  int server_fd, client_fd;
  create_socketpair(server_fd, client_fd);

  // Enqueue 3 events with id="1", "2", "3"
  for (int i = 1; i <= 3; i++) {
    SSEEvent event;
    sse_event_init(&event);
    snprintf(event.id, sizeof(event.id), "%d", i);
    snprintf(event.data, sizeof(event.data), "event%d", i);
    sse_stream_enqueue_event(&stream, &event);
  }

  // Replay events after last_event_id="1" -> should send id="2" and id="3"
  size_t replayed = sse_stream_replay_events(&stream, server_fd, "1");

  EXPECT_EQ(replayed, 2u);

  // Read all data from the client side
  char buf[SSE_SERIALIZE_BUFFER_CAPACITY * 3] = {0};
  ssize_t n = recv(client_fd, buf, sizeof(buf) - 1, 0);

  EXPECT_GT(n, 0);

  // Should contain event2 and event3 but not event1
  EXPECT_EQ(strstr(buf, "event1"), nullptr);
  EXPECT_NE(strstr(buf, "event2"), nullptr);
  EXPECT_NE(strstr(buf, "event3"), nullptr);

  close(server_fd);
  close(client_fd);
}
