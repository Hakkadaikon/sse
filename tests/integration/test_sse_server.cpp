extern "C" {
#include "sse/sse_event.h"
#include "sse/sse_conn.h"
#include "sse/sse_stream.h"
#include "http/http.h"
}

// Undefine macros from C headers that conflict with C++ standard library
#undef value
#undef is_null
#undef is_empty
#undef strlen
#undef strnlen
#undef strncmp

#include <gtest/gtest.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <string>

class SSEServerTest : public ::testing::Test {
protected:
  int server_fd;
  int client_fd;

  void SetUp() override {
    int fds[2];
    ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));
    server_fd = fds[0];
    client_fd = fds[1];
  }

  void TearDown() override {
    close(server_fd);
    close(client_fd);
  }

  std::string recvAll(int fd, size_t expected_len) {
    std::string result;
    result.resize(expected_len);
    ssize_t n = recv(fd, &result[0], expected_len, 0);
    if (n > 0) {
      result.resize(static_cast<size_t>(n));
    } else {
      result.clear();
    }
    return result;
  }
};

// ---------------------------------------------------------------------------
// 1. SSEResponseHeaderReachesClient
//    サーバー側fdでsse_build_response_headerの結果をsend、
//    クライアント側fdでrecvしてtext/event-streamが含まれることを確認
// ---------------------------------------------------------------------------
TEST_F(SSEServerTest, SSEResponseHeaderReachesClient) {
  // Arrange
  char   header_buf[SSE_SERIALIZE_BUFFER_CAPACITY];
  size_t header_len = sse_build_response_header(header_buf, sizeof(header_buf));
  ASSERT_GT(header_len, 0u);

  // Act
  ssize_t sent = send(server_fd, header_buf, header_len, 0);
  ASSERT_EQ(static_cast<ssize_t>(header_len), sent);

  std::string received = recvAll(client_fd, header_len);

  // Assert
  EXPECT_NE(std::string::npos, received.find("text/event-stream"));
  EXPECT_NE(std::string::npos, received.find("HTTP/1.1 200 OK"));
  EXPECT_NE(std::string::npos, received.find("Cache-Control: no-cache"));
  EXPECT_NE(std::string::npos, received.find("Connection: keep-alive"));
}

// ---------------------------------------------------------------------------
// 2. EventReachesClientInSSEFormat
//    サーバー側でSSEイベントをシリアライズしてsend、
//    クライアント側でrecvして"data:hello\n\n"形式で届くことを確認
// ---------------------------------------------------------------------------
TEST_F(SSEServerTest, EventReachesClientInSSEFormat) {
  // Arrange
  SSEEvent event;
  sse_event_init(&event);
  std::memcpy(event.data, "hello", 6);

  char   buf[SSE_SERIALIZE_BUFFER_CAPACITY];
  size_t len = sse_event_serialize(&event, buf, sizeof(buf));
  ASSERT_GT(len, 0u);

  // Act
  ssize_t sent = send(server_fd, buf, len, 0);
  ASSERT_EQ(static_cast<ssize_t>(len), sent);

  std::string received = recvAll(client_fd, len);

  // Assert
  EXPECT_EQ(std::string("data:hello\n\n"), received);
}

// ---------------------------------------------------------------------------
// 3. MultipleEventsReachClient
//    2つのイベントを連続送信し、クライアント側で両方受信できることを確認
// ---------------------------------------------------------------------------
TEST_F(SSEServerTest, MultipleEventsReachClient) {
  // Arrange
  SSEEvent event1;
  sse_event_init(&event1);
  std::memcpy(event1.data, "first", 6);

  SSEEvent event2;
  sse_event_init(&event2);
  std::memcpy(event2.data, "second", 7);

  char   buf1[SSE_SERIALIZE_BUFFER_CAPACITY];
  size_t len1 = sse_event_serialize(&event1, buf1, sizeof(buf1));
  ASSERT_GT(len1, 0u);

  char   buf2[SSE_SERIALIZE_BUFFER_CAPACITY];
  size_t len2 = sse_event_serialize(&event2, buf2, sizeof(buf2));
  ASSERT_GT(len2, 0u);

  // Act
  ssize_t sent1 = send(server_fd, buf1, len1, 0);
  ASSERT_EQ(static_cast<ssize_t>(len1), sent1);

  ssize_t sent2 = send(server_fd, buf2, len2, 0);
  ASSERT_EQ(static_cast<ssize_t>(len2), sent2);

  std::string received = recvAll(client_fd, len1 + len2);

  // Assert
  EXPECT_NE(std::string::npos, received.find("data:first\n\n"));
  EXPECT_NE(std::string::npos, received.find("data:second\n\n"));
  EXPECT_EQ(len1 + len2, received.size());
}

// ---------------------------------------------------------------------------
// 4. CommentKeepAliveReachesClient
//    コメント行(": keepalive\n")をsendし、クライアント側で受信確認
// ---------------------------------------------------------------------------
TEST_F(SSEServerTest, CommentKeepAliveReachesClient) {
  // Arrange
  const char* comment     = "keepalive";
  size_t      comment_len = std::strlen(comment);

  char   buf[SSE_SERIALIZE_BUFFER_CAPACITY];
  size_t len = sse_serialize_comment(comment, comment_len, buf, sizeof(buf));
  ASSERT_GT(len, 0u);

  // Act
  ssize_t sent = send(server_fd, buf, len, 0);
  ASSERT_EQ(static_cast<ssize_t>(len), sent);

  std::string received = recvAll(client_fd, len);

  // Assert
  EXPECT_EQ(std::string(": keepalive\n"), received);
}

// ---------------------------------------------------------------------------
// 5. LastEventIdExtractedFromRequest
//    HTTPリクエスト文字列をsend、サーバー側でrecv -> extract_http_request
//    -> sse_conn_extract_last_event_idの一連の流れを検証
// ---------------------------------------------------------------------------
TEST_F(SSEServerTest, LastEventIdExtractedFromRequest) {
  // Arrange
  const char http_request[] =
    "GET /events HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "Accept: text/event-stream\r\n"
    "Last-Event-ID: evt-42\r\n"
    "\r\n";
  size_t request_len = std::strlen(http_request);

  // Act: クライアント側からHTTPリクエストを送信
  ssize_t sent = send(client_fd, http_request, request_len, 0);
  ASSERT_EQ(static_cast<ssize_t>(request_len), sent);

  // サーバー側でrecv
  char    recv_buf[4096];
  ssize_t n = recv(server_fd, recv_buf, sizeof(recv_buf), 0);
  ASSERT_GT(n, 0);

  // extract_http_requestでパース
  HTTPRequest parsed;
  std::memset(&parsed, 0, sizeof(parsed));
  bool parse_ok = extract_http_request(recv_buf, static_cast<size_t>(n), &parsed);
  ASSERT_TRUE(parse_ok);

  // sse_conn_extract_last_event_idでLast-Event-IDを取得
  SSEConnection conn;
  sse_conn_init(&conn);
  sse_conn_open(&conn, server_fd);

  bool extracted = sse_conn_extract_last_event_id(
    &conn, parsed.headers, parsed.header_size);

  // Assert
  EXPECT_TRUE(extracted);
  EXPECT_STREQ("evt-42", conn.last_event_id);
}
