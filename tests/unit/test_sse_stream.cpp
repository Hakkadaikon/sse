extern "C" {
#include "sse/sse_stream.h"
}
#undef value
#include <gtest/gtest.h>
#include <cstring>

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
