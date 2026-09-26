#!/bin/bash

TEST_FILTER="${1:-}"

cmake --build build -j$(nproc)

if [ -n "$TEST_FILTER" ]; then
    TEST_TORRENTS_DIR=$(pwd)/test/metainfo/torrents ctest --test-dir build --output-on-failure --verbose -R "$TEST_FILTER"
else
    TEST_TORRENTS_DIR=$(pwd)/test/metainfo/torrents ctest --test-dir build --output-on-failure --verbose
fi
