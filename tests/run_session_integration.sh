#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
binary="$(mktemp /tmp/brisas-session-test.XXXXXX)"
trap 'rm -f "$binary"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -pthread -Wall -Wextra -Werror \
    -I../../../libs/json/include -Isrc tests/session_integration.cpp \
    src/SessionProtocol.cpp src/SessionWebSocketClient.cpp src/KinectRecorder.cpp \
    src/RecordingTransfer.cpp -lPocoNet -lPocoFoundation -o "$binary"
python3 tests/validate_session_events.py "$binary"
python3 tests/validate_session_integration.py "$binary"
