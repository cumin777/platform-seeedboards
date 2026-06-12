#
# Copyright (c) 2025 Nordic Semiconductor ASA
# Copyright (c) 2026 Seeed Studio
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#

cmake_minimum_required(VERSION 3.20.0)

# Zephyr CMake project
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nrf_edgeai_anomaly_sample)

# Source files
target_include_directories(app PRIVATE include)

target_sources(app PRIVATE
    src/main.c
    src/nrf_edgeai_generated/nrf_edgeai_user_model.c
)

target_include_directories(app PRIVATE src/nrf_edgeai_generated)
