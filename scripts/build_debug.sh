#!/bin/bash
set -e

cd "$(dirname "$0")/.."

cmake -S . -B debug -DCMAKE_BUILD_TYPE=Debug "$@"
cmake --build debug -j$(nproc)
