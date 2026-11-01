#!/usr/bin/env bash
# ProsperoTV - Container playback tests against the pinned production FFmpeg.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
build="$root/build/media-tests"
mkdir -p "$build"
bash tools/make-media-fixtures.sh
if ! bash tools/setup-audio-dependencies.sh host > "$build/dependencies.log" 2>&1; then
    tail -60 "$build/dependencies.log" >&2
    exit 1
fi
ffmpeg="$root/.deps/ffmpeg-host/root"
gtest=$(bash tools/setup-test-dependencies.sh | tail -1)
# The live history (iptv_timeshift.cpp, for the MPEG-2 size change) keeps VP9 packets too: its
# C half is built as C, by the C compiler beside HOST_CXX.
compiler=${HOST_CXX:-clang++}
"${HOST_CC:-${compiler/clang++/clang}}" -std=c11 -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -Werror -Iinclude \
    -c src/iptv_vp9_packet.c -o "$build/iptv-vp9-packet.o"
"${HOST_CXX:-clang++}" -std=c++20 -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
    -Wall -Wextra -Wpedantic -Werror -pthread -Iinclude -Iopengl-ui/src -I"$ffmpeg/include" \
    -isystem "$gtest/googletest/include" -I"$gtest/googletest" \
    tests/test_iptv_media.cpp tests/test_iptv_subtitles.cpp tests/test_iptv_fields.cpp \
    tests/test_iptv_audio_tempo.cpp tests/test_iptv_recording_transport.cpp \
    src/iptv_media.cpp src/iptv_stream.cpp src/iptv_subtitles.cpp src/iptv_fields.cpp \
    src/iptv_audio_tempo.cpp src/iptv_http.cpp src/iptv_catalog.cpp src/iptv_timeshift.cpp \
    "$build/iptv-vp9-packet.o" \
    "$gtest/googletest/src/gtest-all.cc" "$gtest/googletest/src/gtest_main.cc" \
    "$ffmpeg/lib/libavfilter.a" "$ffmpeg/lib/libavformat.a" "$ffmpeg/lib/libavcodec.a" "$ffmpeg/lib/libswresample.a" "$ffmpeg/lib/libavutil.a" \
    -lssl -lcrypto -lm -o "$build/media_tests"
ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1 "$build/media_tests" "$@"
