cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xiao_nrf54lm20b_edgeai_gesture_data_collection)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_sources(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/main.c
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/imu_lsm6dsl.c)
