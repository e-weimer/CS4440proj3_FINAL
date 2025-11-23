#!/bin/bash
# Test script for Problem 5 directory client (fs_dirs).
#
# Run this inside a `script` session, e.g.:
#   script fs_dirs_tests.typescript
#   ./test_fs_dirs.sh
#   exit
#
# Assumes the following executables exist in the current directory:
#   disk_server, fs_server, fs_cli, fs_dirs

set -uo pipefail

DISK_PORT=5600
FS_PORT=5601
DISK_IMG="fs_dirs_test_disk.img"

DISK_SERVER_PID=""
FS_SERVER_PID=""

cleanup() {
    echo
    echo "Cleaning up..."
    if [[ -n "$FS_SERVER_PID" ]]; then
        kill "$FS_SERVER_PID" 2>/dev/null || true
    fi
    if [[ -n "$DISK_SERVER_PID" ]]; then
        kill "$DISK_SERVER_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

echo "=========== FS_DIRS TESTS BEGIN ==========="
echo

echo "1) Starting disk_server on port $DISK_PORT ..."
./disk_server "$DISK_PORT" 32 32 1000 "$DISK_IMG" &
DISK_SERVER_PID=$!
sleep 1

echo "2) Starting fs_server on port $FS_PORT ..."
./fs_server "$FS_PORT" 127.0.0.1 "$DISK_PORT" &
FS_SERVER_PID=$!
sleep 1
echo

echo "3) Format filesystem using fs_cli (F):"
./fs_cli 127.0.0.1 "$FS_PORT" <<EOF
F
EOF
echo

echo "4) Basic mkdir / cd / pwd sequence:"
./fs_dirs 127.0.0.1 "$FS_PORT" <<EOF
pwd
mkdir foo
cd foo
pwd
mkdir bar
cd bar
pwd
cd /
pwd
exit
EOF
echo

echo "5) mkdir existing directory (expect error on second mkdir foo):"
./fs_dirs 127.0.0.1 "$FS_PORT" <<EOF
mkdir foo
mkdir foo
exit
EOF
echo

echo "6) rmdir non-empty directory (expect 'not empty' error):"
./fs_dirs 127.0.0.1 "$FS_PORT" <<EOF
rmdir foo
exit
EOF
echo

echo "7) Remove nested directory, then parent (expect both rmdir to succeed):"
./fs_dirs 127.0.0.1 "$FS_PORT" <<EOF
cd foo
rmdir bar
cd ..
rmdir foo
pwd
exit
EOF
echo

echo "8) cd into non-existent directory (expect error):"
./fs_dirs 127.0.0.1 "$FS_PORT" <<EOF
cd does_not_exist
pwd
exit
EOF
echo

echo "=========== FS_DIRS TESTS COMPLETE =========="

