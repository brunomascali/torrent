#!/bin/bash

cmake --build build -j$(nproc)
TEST_TORRENTS_DIR=$(pwd)/test/metainfo/torrents ctest --test-dir build --output-on-failure --verbose
