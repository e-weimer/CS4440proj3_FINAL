#!/usr/bin/env bash
set -euo pipefail

PORT=5561

echo "Compiling ls_server and ls_client..."
gcc -Wall -Wextra -o ls_server ls_server.c
gcc -Wall -Wextra -o ls_client ls_client_v2.c

echo
echo "========== LS CLIENT NEGATIVE TESTS =========="

echo
echo "1) Missing args (should print usage and exit non-zero):"
if ./ls_client ; then
    echo "!! ERROR: ls_client returned success but should have failed"
else
    echo "-- OK: ls_client failed as expected"
fi

echo
echo "2) Only host + port (no ls args):"
if ./ls_client 127.0.0.1 "${PORT}" ; then
    echo "!! ERROR: ls_client returned success but should have failed"
else
    echo "-- OK: ls_client failed as expected"
fi

echo
echo "3) Invalid port 0:"
if ./ls_client 127.0.0.1 0 . ; then
    echo "!! ERROR: ls_client returned success but should have failed"
else
    echo "-- OK: ls_client rejected invalid port 0"
fi

echo
echo "4) Connect when no server is listening (connect() should fail):"
if ./ls_client 127.0.0.1 "${PORT}" . ; then
    echo "!! WARNING: ls_client reported success but connect should have failed"
else
    echo "-- OK: ls_client handled connect failure"
fi

echo
echo "========== LS CLIENT FUNCTIONAL TESTS (WITH SERVER) =========="

echo "Starting ls_server on port ${PORT} in background..."
./ls_server "${PORT}" &
SERVER_PID=$!
sleep 1

echo
echo "5) Simple listing of current directory:"
./ls_client 127.0.0.1 "${PORT}" .

echo
echo "6) Detailed listing with options (-l -a .):"
./ls_client 127.0.0.1 "${PORT}" -l -a .

echo
echo "7) Listing a specific directory (/tmp):"
./ls_client 127.0.0.1 "${PORT}" /tmp

echo
echo "8) Invalid path (should print ls error via server):"
./ls_client 127.0.0.1 "${PORT}" /this/path/does/not/exist

echo
echo "Stopping ls_server (SIGTERM)..."
kill "${SERVER_PID}" 2>/dev/null || true
wait "${SERVER_PID}" 2>/dev/null || true

echo
echo "========== LS CLIENT TESTS COMPLETE =========="

