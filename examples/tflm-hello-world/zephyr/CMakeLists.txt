#
# Copyright (c) 2026 Seeed Studio
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tflm_hello_world)

# Disable thread-safe statics for C++ (required by TFLM)
set(NO_THREADSAFE_STATICS $<TARGET_PROPERTY:compiler-cpp,no_threadsafe_statics>)
zephyr_compile_options($<$<COMPILE_LANGUAGE:CXX>:${NO_THREADSAFE_STATICS}>)

# Pick up all C and C++ source files from ../src/
file(GLOB app_sources ../src/*.c ../src/*.cpp)
target_sources(app PRIVATE ${app_sources})
