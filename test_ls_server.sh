#!/usr/bin/env bash
set -euo pipefail

PORT=5560

echo "Compiling ls_server and ls_client..."
gcc -Wall -Wextra -o ls_server ls_server.c
gcc -Wall -Wextra -o ls_client ls_client_v2.c

echo
echo "========== LS SERVER NEGATIVE TESTS =========="

echo
echo "1) Missing port argument (should print usage and exit non-zero):"
if ./ls_server ; then
    echo "!! ERROR: ls_server returned success but should have failed"
else
    echo "-- OK: ls_server failed as expected"
fi

echo
echo "2) Invalid port 0 (should print error and exit non-zero):"
if ./ls_server 0 ; then
    echo "!! ERROR: ls_server returned success but should have failed"
else
    echo "-- OK: ls_server rejected invalid port 0"
fi

echo
echo "3) Invalid port -1 (should print error and exit non-zero):"
if ./ls_server -1 ; then
    echo "!! ERROR: ls_server returned success but should have failed"
else
    echo "-- OK: ls_server rejected invalid port -1"
fi

echo
echo "========== LS SERVER FUNCTIONAL TESTS =========="

echo
echo "Starting ls_server on port ${PORT} in background..."
./ls_server "${PORT}" &
SERVER_PID=$!
sleep 1

echo
echo "4) Simple directory listing ('.'):"
./ls_client 127.0.0.1 "${PORT}" .

echo
echo "5) Long listing with options ('-l .'):"
./ls_client 127.0.0.1 "${PORT}" -l .

echo
echo "6) Listing multiple paths ('. .. /tmp'):"
./ls_client 127.0.0.1 "${PORT}" . .. /tmp

echo
echo "7) Invalid ls option (should show error text coming from server):"
./ls_client 127.0.0.1 "${PORT}" --this-option-does-not-exist

echo
echo "8) Concurrent clients (fork handling and robustness):"
for i in {1..5}; do
    ./ls_client 127.0.0.1 "${PORT}" -l . &
done
wait

echo
echo "Stopping ls_server (SIGTERM)..."
kill "${SERVER_PID}" 2>/dev/null || true
wait "${SERVER_PID}" 2>/dev/null || true

echo
echo "========== LS SERVER TESTS COMPLETE =========="

