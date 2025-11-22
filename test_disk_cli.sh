#!/usr/bin/env bash
set -euo pipefail

CC=gcc
CFLAGS="-Wall -Wextra -g"

PORT=5571
CYL=8
SEC=16
TRACK_US=500
BACKING_FILE="disk_cli.img"

echo "Compiling disk_server and disk_cli..."
$CC $CFLAGS -o disk_server disk_server_v2.c
$CC $CFLAGS -o disk_cli    disk_cli_v2.c

echo
echo "=========== DISK CLI NEGATIVE TESTS ==========="

echo
echo "1) Missing arguments (should print usage and exit non-zero):"
if ./disk_cli ; then
  echo "!! ERROR: disk_cli returned success but should have failed"
else
  echo "-- OK: disk_cli failed as expected (missing args)"
fi

echo
echo "2) Invalid port (0) (should print error and exit non-zero):"
if ./disk_cli 127.0.0.1 0 ; then
  echo "!! ERROR: disk_cli accepted invalid port"
else
  echo "-- OK: disk_cli rejected invalid port 0"
fi

echo
echo "3) Connection failure (no server listening):"
if ./disk_cli 127.0.0.1 "$PORT" <<EOF
I
EOF
then
  echo "!! WARNING: disk_cli reported success when connect should fail"
else
  echo "-- OK: disk_cli handled connect() failure"
fi

echo
echo "=========== DISK CLI FUNCTIONAL TESTS ==========="

echo
echo "4) Start disk_server in background..."
./disk_server "$PORT" "$CYL" "$SEC" "$TRACK_US" "$BACKING_FILE" &
SERVER_PID=$!
sleep 1

echo
echo "5) Geometry + valid read/write commands:"
./disk_cli 127.0.0.1 "$PORT" <<EOF
I
W 0 1 5
HELLO
R 0 1
EOF

echo
echo "6) Invalid command format (unknown command 'X') – client should complain but not crash:"
./disk_cli 127.0.0.1 "$PORT" <<EOF
X 0 0
I
EOF

echo
echo "7) Write with small l (partial sector) followed by readback:"
./disk_cli 127.0.0.1 "$PORT" <<EOF
W 2 3 3
xyz
R 2 3
EOF

echo
echo "Stopping disk_server (SIGTERM)..."
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true

echo
echo "=========== DISK CLI TESTS COMPLETE ==========="

