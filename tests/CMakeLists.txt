cmake_minimum_required(VERSION 3.14)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
)
FetchContent_MakeAvailable(googletest)

enable_testing()

# SSE source files for tests
set(SSE_SOURCES
  ${CMAKE_SOURCE_DIR}/src/sse/sse_event.c
  ${CMAKE_SOURCE_DIR}/src/sse/sse_conn.c
  ${CMAKE_SOURCE_DIR}/src/sse/sse_stream.c
  ${CMAKE_SOURCE_DIR}/src/util/log.c
)

# Unit tests
add_executable(unit_tests
  unit/test_sse_event.cpp
  unit/test_sse_conn.cpp
  unit/test_sse_stream.cpp
  ${SSE_SOURCES}
)
target_link_libraries(unit_tests PRIVATE gtest_main asm)
target_include_directories(unit_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(unit_tests PRIVATE LOG_LEVEL_ERROR)
target_compile_options(unit_tests PRIVATE -Wno-register)
set_target_properties(unit_tests PROPERTIES LINK_FLAGS "-no-pie")

include(GoogleTest)
gtest_discover_tests(unit_tests)

# Integration tests
set(SSE_INTEGRATION_SOURCES
  ${CMAKE_SOURCE_DIR}/src/sse/sse_event.c
  ${CMAKE_SOURCE_DIR}/src/sse/sse_conn.c
  ${CMAKE_SOURCE_DIR}/src/sse/sse_stream.c
  ${CMAKE_SOURCE_DIR}/src/http/http.c
)

add_executable(integration_tests
  integration/test_sse_server.cpp
  ${SSE_INTEGRATION_SOURCES}
)
target_link_libraries(integration_tests PRIVATE gtest_main asm)
target_include_directories(integration_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(integration_tests PRIVATE LOG_LEVEL_ERROR)
target_compile_options(integration_tests PRIVATE -Wno-register)
set_target_properties(integration_tests PROPERTIES LINK_FLAGS "-no-pie")

gtest_discover_tests(integration_tests)
