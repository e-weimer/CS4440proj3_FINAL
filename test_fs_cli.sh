#!/usr/bin/env bash
set -euo pipefail

CC=gcc
CFLAGS="-Wall -Wextra -g"

DISK_PORT=5610
FS_PORT=5611
DISK_IMG="fs_cli_disk.img"

echo "Compiling disk_server, fs_server, and fs_cli..."
$CC $CFLAGS -pthread -o disk_server disk_server.c
$CC $CFLAGS -o fs_server fs_server.c
$CC $CFLAGS -o fs_cli    fs_cli_v2.c

echo
echo "=========== FS CLI NEGATIVE TESTS ==========="

echo
echo "1) Missing arguments (should print usage and exit non-zero):"
if ./fs_cli ; then
  echo "!! ERROR: fs_cli returned success but should have failed"
else
  echo "-- OK: fs_cli failed as expected (missing args)"
fi

echo
echo "2) Invalid port (0) (should print error and exit non-zero):"
if ./fs_cli 127.0.0.1 0 ; then
  echo "!! ERROR: fs_cli accepted invalid port"
else
  echo "-- OK: fs_cli rejected port 0"
fi

echo
echo "3) Connection failure (no fs_server running):"
if ./fs_cli 127.0.0.1 "$FS_PORT" <<EOF
F
EOF
then
  echo "!! WARNING: fs_cli reported success when connect should fail"
else
  echo "-- OK: fs_cli handled connect() failure"
fi

echo
echo "=========== STARTING DISK + FS SERVER =========="

echo
echo "4) Start disk_server in background..."
./disk_server "$DISK_PORT" 32 32 1000 "$DISK_IMG" &
DISK_PID=$!
sleep 1

echo
echo "5) Start fs_server in background..."
./fs_server "$FS_PORT" 127.0.0.1 "$DISK_PORT" &
FS_PID=$!
sleep 1

echo
echo "=========== FS CLI FUNCTIONAL TESTS =========="

echo
echo "6) Basic format + create + list:"
./fs_cli 127.0.0.1 "$FS_PORT" <<EOF
F
C alpha
C beta
L 0
L 1
EOF

echo
echo "7) Write and read a small file (check R formatting):"
./fs_cli 127.0.0.1 "$FS_PORT" <<EOF
W alpha 18
Hello, filesystem!
R alpha
EOF

echo
echo "8) Read a non-existent file (R gamma) – expect code 1:"
./fs_cli 127.0.0.1 "$FS_PORT" <<EOF
R gamma
EOF

echo
echo "9) Overwrite a file with different length (shorter) and read again:"
./fs_cli 127.0.0.1 "$FS_PORT" <<EOF
W alpha 4
ABCD
R alpha
EOF

echo
echo "10) Delete a file and confirm it disappears from directory listing:"
./fs_cli 127.0.0.1 "$FS_PORT" <<EOF
D beta
L 0
EOF

echo
echo "=========== STOPPING SERVERS ==========="

echo "Stopping fs_server..."
kill "$FS_PID" 2>/dev/null || true
wait "$FS_PID" 2>/dev/null || true

echo "Stopping disk_server..."
kill "$DISK_PID" 2>/dev/null || true
wait "$DISK_PID" 2>/dev/null || true

echo
echo "=========== FS CLI TESTS COMPLETE =========="

