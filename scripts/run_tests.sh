#!/bin/bash

cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure --verbose
