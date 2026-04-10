extern "C" {
#include "sse/sse_conn.h"
#include "http/http.h"
}
#undef value
#include <gtest/gtest.h>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

class SSEConnTest : public ::testing::Test {
protected:
  SSEConnection conn;

  void SetUp() override {
    memset(&conn, 0xFF, sizeof(conn));
  }
};

TEST_F(SSEConnTest, InitSetsDefaultValues) {
  sse_conn_init(&conn);

  EXPECT_EQ(conn.fd, -1);
  EXPECT_EQ(conn.state, SSE_CONN_STATE_INACTIVE);
  EXPECT_STREQ(conn.last_event_id, "");
  EXPECT_FALSE(conn.header_sent);
}

TEST_F(SSEConnTest, OpenSetsFdAndActive) {
  sse_conn_init(&conn);
  int32_t test_fd = 42;

  bool result = sse_conn_open(&conn, test_fd);

  EXPECT_TRUE(result);
  EXPECT_EQ(conn.fd, test_fd);
  EXPECT_EQ(conn.state, SSE_CONN_STATE_ACTIVE);
}

TEST_F(SSEConnTest, ExtractLastEventId_WhenPresent) {
  sse_conn_init(&conn);
  sse_conn_open(&conn, 10);

  HTTPRequestHeaderLine headers[3];
  memset(headers, 0, sizeof(headers));

  strncpy(headers[0].key,   "Host",           HTTP_HEADER_KEY_CAPACITY - 1);
  strncpy(headers[0].value, "example.com",    HTTP_HEADER_VALUE_CAPACITY - 1);
  strncpy(headers[1].key,   "Last-Event-ID",  HTTP_HEADER_KEY_CAPACITY - 1);
  strncpy(headers[1].value, "evt-123",        HTTP_HEADER_VALUE_CAPACITY - 1);
  strncpy(headers[2].key,   "Accept",         HTTP_HEADER_KEY_CAPACITY - 1);
  strncpy(headers[2].value, "text/event-stream", HTTP_HEADER_VALUE_CAPACITY - 1);

  bool result = sse_conn_extract_last_event_id(&conn, headers, 3);

  EXPECT_TRUE(result);
  EXPECT_STREQ(conn.last_event_id, "evt-123");
}

TEST_F(SSEConnTest, ExtractLastEventId_WhenAbsent) {
  sse_conn_init(&conn);
  sse_conn_open(&conn, 10);

  HTTPRequestHeaderLine headers[2];
  memset(headers, 0, sizeof(headers));

  strncpy(headers[0].key,   "Host",        HTTP_HEADER_KEY_CAPACITY - 1);
  strncpy(headers[0].value, "example.com", HTTP_HEADER_VALUE_CAPACITY - 1);
  strncpy(headers[1].key,   "Accept",      HTTP_HEADER_KEY_CAPACITY - 1);
  strncpy(headers[1].value, "text/event-stream", HTTP_HEADER_VALUE_CAPACITY - 1);

  bool result = sse_conn_extract_last_event_id(&conn, headers, 2);

  EXPECT_FALSE(result);
  EXPECT_STREQ(conn.last_event_id, "");
}

TEST_F(SSEConnTest, CloseResetsToInactive) {
  sse_conn_init(&conn);
  sse_conn_open(&conn, 7);

  sse_conn_close(&conn);

  EXPECT_EQ(conn.state, SSE_CONN_STATE_INACTIVE);
  EXPECT_EQ(conn.fd, -1);
  EXPECT_FALSE(conn.header_sent);
}

TEST_F(SSEConnTest, IsActive_WhenActive) {
  sse_conn_init(&conn);
  sse_conn_open(&conn, 5);

  bool result = sse_conn_is_active(&conn);

  EXPECT_TRUE(result);
}

TEST_F(SSEConnTest, IsActive_WhenInactive) {
  sse_conn_init(&conn);

  bool result = sse_conn_is_active(&conn);

  EXPECT_FALSE(result);
}

TEST_F(SSEConnTest, SendHeader_Success) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  sse_conn_init(&conn);
  sse_conn_open(&conn, fds[0]);

  bool result = sse_conn_send_header(&conn);
  EXPECT_TRUE(result);

  char recv_buf[1024] = {};
  ssize_t n = recv(fds[1], recv_buf, sizeof(recv_buf) - 1, 0);
  ASSERT_GT(n, 0);
  recv_buf[n] = '\0';

  EXPECT_NE(strstr(recv_buf, "text/event-stream"), nullptr);

  close(fds[0]);
  close(fds[1]);
}

TEST_F(SSEConnTest, SendEvent_Success) {
  int fds[2];
  ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds), 0);

  sse_conn_init(&conn);
  sse_conn_open(&conn, fds[0]);

  SSEEvent event;
  sse_event_init(&event);
  strncpy(event.data, "hello", SSE_EVENT_DATA_CAPACITY - 1);

  bool result = sse_conn_send_event(&conn, &event);
  EXPECT_TRUE(result);

  char recv_buf[1024] = {};
  ssize_t n = recv(fds[1], recv_buf, sizeof(recv_buf) - 1, 0);
  ASSERT_GT(n, 0);
  recv_buf[n] = '\0';

  EXPECT_NE(strstr(recv_buf, "data:hello\n"), nullptr);

  close(fds[0]);
  close(fds[1]);
}
