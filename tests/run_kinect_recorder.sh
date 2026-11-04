#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
output="${1:-$(mktemp -d /tmp/brisas-recorder-test.XXXXXX)}"
binary="$(mktemp /tmp/brisas-recorder-binary.XXXXXX)"
trap 'rm -f "$binary"' EXIT
"${CXX:-c++}" -std=c++17 -O2 -pthread -Wall -Wextra -Werror \
    -I../../../libs/json/include -Isrc tests/kinect_recorder.cpp src/KinectRecorder.cpp -o "$binary"
"$binary" "$output" "${2:-4.25}"
python3 tests/validate_kinect_recording.py "$output"
