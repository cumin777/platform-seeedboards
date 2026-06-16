cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(tflm_person_detection)

set(NO_THREADSAFE_STATICS $<TARGET_PROPERTY:compiler-cpp,no_threadsafe_statics>)
zephyr_compile_options($<$<COMPILE_LANGUAGE:CXX>:${NO_THREADSAFE_STATICS}>)

file(GLOB app_sources ../src/*.c ../src/*.cpp)
target_sources(app PRIVATE ${app_sources})
