#!/usr/bin/env bash
set -euo pipefail

PORT=5555

echo "Compiling server and client..."
gcc -Wall -Wextra -pthread -o server server_v2.c
gcc -Wall -Wextra -o client client_v2.c

echo
echo "========== SERVER NEGATIVE TESTS =========="

echo
echo "1) Missing port argument (should print usage and exit non-zero):"
if ./server ; then
    echo "!! ERROR: server returned success but should have failed"
else
    echo "-- OK: server failed as expected"
fi

echo
echo "2) Invalid port 0 (should print error and exit non-zero):"
if ./server 0 ; then
    echo "!! ERROR: server returned success but should have failed"
else
    echo "-- OK: server rejected invalid port"
fi

echo
echo "3) Invalid port -1 (should print error and exit non-zero):"
if ./server -1 ; then
    echo "!! ERROR: server returned success but should have failed"
else
    echo "-- OK: server rejected invalid port"
fi

echo
echo "========== SERVER NORMAL / CONCURRENCY TESTS =========="

# Start server in background for functional tests
echo
echo "Starting server on port ${PORT} in background..."
./server "${PORT}" &
SERVER_PID=$!
sleep 1   # give the server time to start

echo
echo "4) Single normal client request:"
./client 127.0.0.1 "${PORT}" "hello world"

echo
echo "5) Long input line (buffer boundary / robustness):"
LONG_STR=$(printf 'x%.0s' {1..3000})
./client 127.0.0.1 "${PORT}" "${LONG_STR}" | head -c 80 ; echo " ..."

echo
echo "6) Multiple concurrent clients (demonstrate multi-threading & DoS behavior):"
for i in {1..8}; do
    ./client 127.0.0.1 "${PORT}" "message $i from concurrent client" &
done
wait

echo
echo "Stopping server (SIGTERM)..."
kill "${SERVER_PID}" 2>/dev/null || true
wait "${SERVER_PID}" 2>/dev/null || true

echo
echo "========== SERVER TESTS COMPLETE =========="

