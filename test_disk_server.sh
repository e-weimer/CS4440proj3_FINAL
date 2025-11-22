#!/usr/bin/env bash
set -euo pipefail

CC=gcc
CFLAGS="-Wall -Wextra -g"

PORT=5570
CYL=10
SEC=20
TRACK_US=1000
BACKING_FILE="disk.img"

echo "Compiling disk_server, disk_cli, and disk_rand..."
$CC $CFLAGS -o disk_server disk_server_v2.c
$CC $CFLAGS -o disk_cli    disk_cli_v2.c
$CC $CFLAGS -o disk_rand   disk_rand_v2.c

echo
echo "=========== DISK SERVER NEGATIVE TESTS ==========="

echo
echo "1) Missing arguments (should print usage and exit non-zero):"
if ./disk_server ; then
  echo "!! ERROR: disk_server returned success but should have failed"
else
  echo "-- OK: disk_server failed as expected (missing args)"
fi

echo
echo "2) Invalid cylinders = 0 (should print error and exit non-zero):"
if ./disk_server "$PORT" 0 "$SEC" "$TRACK_US" "$BACKING_FILE" ; then
  echo "!! ERROR: disk_server accepted invalid cylinder count"
else
  echo "-- OK: disk_server rejected cylinders = 0"
fi

echo
echo "3) Invalid sectors = 0 (should print error and exit non-zero):"
if ./disk_server "$PORT" "$CYL" 0 "$TRACK_US" "$BACKING_FILE" ; then
  echo "!! ERROR: disk_server accepted invalid sector count"
else
  echo "-- OK: disk_server rejected sectors = 0"
fi

echo
echo "=========== DISK SERVER FUNCTIONAL TESTS ==========="

echo
echo "4) Starting disk_server in background..."
./disk_server "$PORT" "$CYL" "$SEC" "$TRACK_US" "$BACKING_FILE" &
SERVER_PID=$!
sleep 1

echo
echo "5) Query geometry via disk_cli (I command):"
./disk_cli 127.0.0.1 "$PORT" <<EOF
I
EOF

echo
echo "6) Simple write and readback (W then R of same block):"
./disk_cli 127.0.0.1 "$PORT" <<EOF
W 0 0 4
ABCD
R 0 0
EOF

echo
echo "7) Read from an out-of-range block (R 99 99) – server should return 0:"
./disk_cli 127.0.0.1 "$PORT" <<EOF
R 99 99
EOF

echo
echo "8) Exercise concurrent access using disk_rand (short run):"
./disk_rand 127.0.0.1 "$PORT" 64 12345

echo
echo "Stopping disk_server (SIGTERM)..."
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true

echo
echo "=========== DISK SERVER TESTS COMPLETE ==========="

