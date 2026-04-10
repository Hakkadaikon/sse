extern "C" {
#include "sse/sse_event.h"
}
#undef value
#include <gtest/gtest.h>
#include <cstring>

class SSEEventTest : public ::testing::Test {
protected:
  SSEEvent event;

  void SetUp() override {
    std::memset(&event, 0xFF, sizeof(event));
  }
};

// 1. Init sets all fields to default values
TEST_F(SSEEventTest, InitSetsAllFieldsToZero) {
  bool result = sse_event_init(&event);

  EXPECT_TRUE(result);
  EXPECT_STREQ(event.data, "");
  EXPECT_STREQ(event.event_type, "");
  EXPECT_STREQ(event.id, "");
  EXPECT_EQ(event.retry_ms, SSE_RETRY_UNSET);
}

// 2. Serialize with data only
TEST_F(SSEEventTest, SerializeWithDataOnly) {
  sse_event_init(&event);
  std::strcpy(event.data, "hello");

  char   buffer[SSE_SERIALIZE_BUFFER_CAPACITY] = {};
  size_t written = sse_event_serialize(&event, buffer, sizeof(buffer));

  const char* expected = "data:hello\n\n";
  EXPECT_STREQ(buffer, expected);
  EXPECT_EQ(written, std::strlen(expected));
}

// 3. Serialize splits data on newlines
TEST_F(SSEEventTest, SerializeWithNewlineInData) {
  sse_event_init(&event);
  std::strcpy(event.data, "hello\nworld");

  char   buffer[SSE_SERIALIZE_BUFFER_CAPACITY] = {};
  size_t written = sse_event_serialize(&event, buffer, sizeof(buffer));

  const char* expected = "data:hello\ndata:world\n\n";
  EXPECT_STREQ(buffer, expected);
  EXPECT_EQ(written, std::strlen(expected));
}

// 4. Serialize with all fields set
TEST_F(SSEEventTest, SerializeWithAllFields) {
  sse_event_init(&event);
  std::strcpy(event.data, "payload");
  std::strcpy(event.event_type, "update");
  std::strcpy(event.id, "42");
  event.retry_ms = 5000;

  char   buffer[SSE_SERIALIZE_BUFFER_CAPACITY] = {};
  size_t written = sse_event_serialize(&event, buffer, sizeof(buffer));

  std::string result(buffer, written);
  EXPECT_NE(result.find("id:42\n"), std::string::npos);
  EXPECT_NE(result.find("event:update\n"), std::string::npos);
  EXPECT_NE(result.find("retry:5000\n"), std::string::npos);
  EXPECT_NE(result.find("data:payload\n"), std::string::npos);
  EXPECT_EQ(result.substr(result.size() - 1), "\n");
  EXPECT_EQ(result.substr(result.size() - 2, 1), "\n");
}

// 5. Serialize with retry field
TEST_F(SSEEventTest, SerializeWithRetry) {
  sse_event_init(&event);
  std::strcpy(event.data, "test");
  event.retry_ms = 3000;

  char   buffer[SSE_SERIALIZE_BUFFER_CAPACITY] = {};
  size_t written = sse_event_serialize(&event, buffer, sizeof(buffer));

  std::string result(buffer, written);
  EXPECT_NE(result.find("retry:3000\n"), std::string::npos);
  EXPECT_NE(result.find("data:test\n"), std::string::npos);
}

// 6. Serialize comment line
TEST_F(SSEEventTest, SerializeComment) {
  const char* comment = "keepalive";
  char        buffer[SSE_SERIALIZE_BUFFER_CAPACITY] = {};

  size_t written = sse_serialize_comment(
    comment, std::strlen(comment), buffer, sizeof(buffer));

  const char* expected = ": keepalive\n";
  EXPECT_STREQ(buffer, expected);
  EXPECT_EQ(written, std::strlen(expected));
}

// 7. Build HTTP response header
TEST_F(SSEEventTest, BuildResponseHeader) {
  char buffer[SSE_SERIALIZE_BUFFER_CAPACITY] = {};

  size_t written = sse_build_response_header(buffer, sizeof(buffer));

  EXPECT_GT(written, static_cast<size_t>(0));
  std::string result(buffer, written);
  EXPECT_NE(result.find("text/event-stream"), std::string::npos);
  EXPECT_NE(result.find("no-cache"), std::string::npos);
  EXPECT_NE(result.find("200"), std::string::npos);
}

// 8. Serialize returns 0 on buffer overflow
TEST_F(SSEEventTest, SerializeBufferOverflow) {
  sse_event_init(&event);
  std::strcpy(event.data, "hello");

  char   tiny_buffer[2] = {};
  size_t written = sse_event_serialize(&event, tiny_buffer, sizeof(tiny_buffer));

  EXPECT_EQ(written, static_cast<size_t>(0));
}
