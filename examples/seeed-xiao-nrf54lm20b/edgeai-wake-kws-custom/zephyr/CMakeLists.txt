# SPDX-License-Identifier: Apache-2.0
#
# Self-contained XIAO nRF54LM20B custom wake word / keyword spotting sample.
# The generated Nordic Edge AI Lab Axon sources are deliberately kept under
# src/ww and src/kws so each wrapper resolves its own model headers. Until a
# custom model is installed, each side falls back independently to the known-
# good reference model from sample 23, allowing hardware bring-up first.

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xiao_nrf54lm20b_custom_wake_kws)

# PlatformIO supplies src/*.c separately, but Zephyr still requires its app
# library to own at least one source while this CMake file is configured.
target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/model_anchor.c)

target_include_directories(app PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../src"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/ww"
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/kws"
)

set(LOCAL_WW_MODEL_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/ww/nrf_edgeai_generated")
set(LOCAL_KWS_MODEL_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/../src/kws/nrf_edgeai_generated")
set(REFERENCE_MODEL_ROOT
  "${CMAKE_CURRENT_SOURCE_DIR}/../../edgeai-wake-words-local/src")
set(WW_MODEL_DIR "${LOCAL_WW_MODEL_DIR}")
set(KWS_MODEL_DIR "${LOCAL_KWS_MODEL_DIR}")

if(NOT EXISTS "${LOCAL_WW_MODEL_DIR}/nrf_edgeai_user_model.h")
  set(WW_MODEL_DIR "${REFERENCE_MODEL_ROOT}/ww/nrf_edgeai_generated")
  message(WARNING "Custom WW model not installed; using sample 23 reference model")
endif()
if(NOT EXISTS "${LOCAL_KWS_MODEL_DIR}/nrf_edgeai_user_model.h")
  set(KWS_MODEL_DIR "${REFERENCE_MODEL_ROOT}/kws/nrf_edgeai_generated")
  message(WARNING "Custom KWS model not installed; using sample 23 reference model")
endif()

target_compile_definitions(app PRIVATE
  WW_MODEL_HEADER=\"${WW_MODEL_DIR}/nrf_edgeai_user_model.h\"
  KWS_MODEL_HEADER=\"${KWS_MODEL_DIR}/nrf_edgeai_user_model.h\"
)

# `board_build.zephyr.app_build_control = true` lets PlatformIO contribute the
# project's `src/*.c` files.  Do not add them here as well: doing so would put
# every application object into both the PlatformIO object list and Zephyr's
# `app` library, causing duplicate symbols at link time.  CMake only adds the
# generated model sources below (which are outside the normal PlatformIO src
# scan).
file(GLOB_RECURSE ww_model_sources CONFIGURE_DEPENDS "${WW_MODEL_DIR}/*.c")
file(GLOB_RECURSE kws_model_sources CONFIGURE_DEPENDS "${KWS_MODEL_DIR}/*.c")

# Both Nordic Edge AI Lab exports contain a source file named
# nrf_edgeai_user_model.c.  They must be separate CMake targets: PlatformIO
# otherwise maps both to the same app/nrf_edgeai_generated object filename.
add_library(edgeai_ww_model STATIC ${ww_model_sources})
add_library(edgeai_kws_model STATIC ${kws_model_sources})
target_include_directories(edgeai_ww_model PRIVATE
  "${ZEPHYR_SDK_EDGE_AI_MODULE_DIR}/include")
target_include_directories(edgeai_kws_model PRIVATE
  "${ZEPHYR_SDK_EDGE_AI_MODULE_DIR}/include")
target_compile_definitions(edgeai_ww_model PRIVATE
  NRF_AXON_INTERLAYER_BUFFER_SIZE=6656
  NRF_AXON_PSUM_BUFFER_SIZE=0)
target_compile_definitions(edgeai_kws_model PRIVATE
  NRF_AXON_INTERLAYER_BUFFER_SIZE=6656
  NRF_AXON_PSUM_BUFFER_SIZE=0)
target_compile_options(edgeai_ww_model PRIVATE
  -mcpu=cortex-m33 -mthumb -mabi=aapcs -mfpu=fpv5-sp-d16 -mfloat-abi=hard)
target_compile_options(edgeai_kws_model PRIVATE
  -mcpu=cortex-m33 -mthumb -mabi=aapcs -mfpu=fpv5-sp-d16 -mfloat-abi=hard)
target_link_libraries(app PRIVATE edgeai_ww_model edgeai_kws_model)
