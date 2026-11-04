#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
binary="$(mktemp /tmp/brisas-transfer-test.XXXXXX)"
trap 'rm -f "$binary"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -pthread -Wall -Wextra -Werror \
    -I../../../libs/json/include -Isrc tests/recording_transfer.cpp \
    src/KinectRecorder.cpp src/RecordingTransfer.cpp -lPocoFoundation -o "$binary"
python3 tests/validate_recording_transfer.py "$binary"
