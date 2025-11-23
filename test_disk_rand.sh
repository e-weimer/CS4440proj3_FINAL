#!/usr/bin/env bash
set -euo pipefail

CC=gcc
CFLAGS="-Wall -Wextra -g"

PORT=5572
CYL=12
SEC=24
TRACK_US=750
BACKING_FILE="disk_rand.img"

echo "Compiling disk_server and disk_rand..."
$CC $CFLAGS -o disk_server disk_server.c
$CC $CFLAGS -o disk_rand   disk_rand.c

echo
echo "=========== DISK RAND NEGATIVE TESTS ==========="

echo
echo "1) Missing arguments (should print usage and exit non-zero):"
if ./disk_rand ; then
  echo "!! ERROR: disk_rand returned success but should have failed"
else
  echo "-- OK: disk_rand failed as expected (missing args)"
fi

echo
echo "2) Invalid N (0) (should print error and exit non-zero):"
if ./disk_rand 127.0.0.1 "$PORT" 0 42 ; then
  echo "!! ERROR: disk_rand accepted N=0"
else
  echo "-- OK: disk_rand rejected N=0"
fi

echo
echo "3) Connection failure (no server listening):"
if ./disk_rand 127.0.0.1 "$PORT" 10 99 ; then
  echo "!! WARNING: disk_rand reported success when connect should fail"
else
  echo "-- OK: disk_rand handled connect() failure"
fi

echo
echo "=========== DISK RAND FUNCTIONAL TESTS ==========="

echo
echo "4) Start disk_server in background..."
./disk_server "$PORT" "$CYL" "$SEC" "$TRACK_US" "$BACKING_FILE" &
SERVER_PID=$!
sleep 1

echo
echo "5) Short random workload (N=32, seed=1234):"
./disk_rand 127.0.0.1 "$PORT" 32 1234

echo
echo "6) Larger random workload (N=256, seed=5678):"
./disk_rand 127.0.0.1 "$PORT" 256 5678

echo
echo "Stopping disk_server (SIGTERM)..."
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true

echo
echo "=========== DISK RAND TESTS COMPLETE ==========="

