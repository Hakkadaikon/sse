#!/bin/bash
#
# SSE client test script
#
# Usage:
#   ./scripts/sse_client.sh [host] [port] [path] [duration_sec]
#
# Examples:
#   ./scripts/sse_client.sh                          # default: localhost:8080/events for 10s
#   ./scripts/sse_client.sh localhost 8080 /events 30 # custom
#

HOST="${1:-localhost}"
PORT="${2:-8080}"
PATH_="${3:-/events}"
DURATION="${4:-10}"

URL="http://${HOST}:${PORT}${PATH_}"

echo "Connecting to ${URL} for ${DURATION}s..."
echo "Press Ctrl+C to stop early."
echo "---"

timeout "${DURATION}" curl -s -N \
  -H "Accept: text/event-stream" \
  -H "Cache-Control: no-cache" \
  "${URL}"

echo ""
echo "---"
echo "Done."
