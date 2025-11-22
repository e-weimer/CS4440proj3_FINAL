#!/usr/bin/env bash
set -euo pipefail

PORT=5556    # separate port so you can run both scripts if needed

echo "Compiling server and client..."
gcc -Wall -Wextra -pthread -o server server_v2.c
gcc -Wall -Wextra -o client client_v2.c

echo
echo "========== CLIENT NEGATIVE TESTS =========="

echo
echo "1) Missing args (should print usage and exit non-zero):"
if ./client ; then
    echo "!! ERROR: client returned success but should have failed"
else
    echo "-- OK: client failed as expected"
fi

echo
echo "2) Too few args (host + port only):"
if ./client 127.0.0.1 "${PORT}" ; then
    echo "!! ERROR: client returned success but should have failed"
else
    echo "-- OK: client failed as expected"
fi

echo
echo "3) Invalid port 0:"
if ./client 127.0.0.1 0 "test" ; then
    echo "!! ERROR: client returned success but should have failed"
else
    echo "-- OK: client rejected invalid port"
fi

echo
echo "4) No server listening (connect should fail gracefully):"
if ./client 127.0.0.1 "${PORT}" "test when no server" ; then
    echo "!! WARNING: client reported success but connect was expected to fail"
else
    echo "-- OK: client handled connect failure"
fi

echo
echo "========== CLIENT NORMAL TESTS (WITH SERVER) =========="

echo "Starting server on port ${PORT} in background..."
./server "${PORT}" &
SERVER_PID=$!
sleep 1

echo
echo "5) Simple string round-trip:"
./client 127.0.0.1 "${PORT}" "abcdefg"

echo
echo "6) String with spaces:"
./client 127.0.0.1 "${PORT}" "this is a test of the emergency broadcast system"

echo
echo "7) Multiple quick successive calls:"
for i in {1..5}; do
    ./client 127.0.0.1 "${PORT}" "quick client call #${i}"
done

echo
echo "Stopping server (SIGTERM)..."
kill "${SERVER_PID}" 2>/dev/null || true
wait "${SERVER_PID}" 2>/dev/null || true

echo
echo "========== CLIENT TESTS COMPLETE =========="

