#!/usr/bin/env bash

shopt -s globstar

clang-format src/**/*.cpp src/**/*.h tests/**/*.cpp tests/**/*.h -i
python -m black . --exclude extern/
