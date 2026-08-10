#!/bin/bash

rm -rf build
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build
