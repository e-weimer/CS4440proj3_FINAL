#!/usr/bin/env bash
set -euo pipefail

CC=gcc
CFLAGS="-Wall -Wextra -g"

# Ports and files
DISK_PORT=5600
FS_PORT=5601
DISK_IMG="fs_disk.img"

echo "Compiling disk_server, fs_server, and fs_cli..."
$CC $CFLAGS -pthread -o disk_server disk_server.c
$CC $CFLAGS -o fs_server fs_server.c
$CC $CFLAGS -o fs_cli    fs_cli.c

echo
echo "=========== FS SERVER NEGATIVE TESTS ==========="

echo
echo "1) Missing arguments (should print usage and exit non-zero):"
if ./fs_server ; then
  echo "!! ERROR: fs_server returned success but should have failed"
else
  echo "-- OK: fs_server failed as expected (missing args)"
fi

echo
echo "2) Too few arguments (1 arg – should fail):"
if ./fs_server "$FS_PORT" ; then
  echo "!! ERROR: fs_server returned success but should have failed"
else
  echo "-- OK: fs_server failed as expected (too few args)"
fi

echo
echo "=========== STARTING DISK + FS SERVER =========="

echo
echo "3) Starting disk_server in background..."
./disk_server "$DISK_PORT" 32 32 1000 "$DISK_IMG" &
DISK_PID=$!
sleep 1

echo
echo "4) Starting fs_server in background..."
./fs_server "$FS_PORT" 127.0.0.1 "$DISK_PORT" &
FS_PID=$!
sleep 1

echo
echo "=========== FS SERVER FUNCTIONAL TESTS ========="

echo
echo "5) Format filesystem and create files (F, C):"
./fs_cli 127.0.0.1 "$FS_PORT" <<EOF
F
C foo
C bar
EOF

echo
echo "6) Create an existing file again (C foo) – expect return code 1:"
./fs_cli 127.0.0.1 "$FS_PORT" <<EOF
C foo
EOF

echo
echo "7) Directory listing: names only (L 0) and with lengths (L 1):"
./fs_cli 127.0.0.1 "$FS_PORT" <<EOF
L 0
L 1
EOF

echo
echo "8) Write and read back a file (W foo, then R foo):"
./fs_cli 127.0.0.1 "$FS_PORT" <<EOF
W foo 18
Hello, filesystem!
R foo
EOF

echo
echo "9) Read non-existent file (R baz) – expect code 1:"
./fs_cli 127.0.0.1 "$FS_PORT" <<EOF
R baz
EOF

echo
echo "10) Delete existing file bar, then delete non-existent baz:"
./fs_cli 127.0.0.1 "$FS_PORT" <<EOF
D bar
D baz
EOF

echo
echo "11) Write to non-existent file qux – expect code 1 / error:"
./fs_cli 127.0.0.1 "$FS_PORT" <<EOF
W qux 3
abc
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
echo "=========== FS SERVER TESTS COMPLETE =========="

