/* SPDX-License-Identifier: Apache-2.0 */
/* Adapted from tflite-micro examples/memory_footprint/interpreter_memory_footprint.cc
 * Measures the TFLM *framework* code footprint (interpreter + memory planner) with
 * NO kernels registered, so the resulting binary size reflects the framework only.
 * By design Invoke() reports an error because no op is registered. */
#include <cstdint>
#include <cstdlib>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "tensorflow/lite/micro/benchmarks/micro_benchmark.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_profiler.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/system_setup.h"

#include "simple_add_model_data.h"

LOG_MODULE_REGISTER(memory_footprint, CONFIG_LOG_DEFAULT_LEVEL);

using InterpreterMemoryFootprintRunner = tflite::MicroBenchmarkRunner<int16_t>;
using InterpreterMemoryFootprintOpResolver = tflite::MicroMutableOpResolver<6>;

int main(void) {
  constexpr int kTensorArenaSize = 1024;
  alignas(16) uint8_t tensor_arena[kTensorArenaSize];
  uint8_t runner_buffer[sizeof(InterpreterMemoryFootprintRunner)];

  tflite::InitializeTarget();
  tflite::MicroProfiler profiler;

  InterpreterMemoryFootprintOpResolver op_resolver;
  /* Do NOT allocate any OP so that the binary does not include any kernels. */

  InterpreterMemoryFootprintRunner* runner = new (runner_buffer)
      InterpreterMemoryFootprintRunner(g_simple_add_model_model_data,
                                       &op_resolver, tensor_arena,
                                       kTensorArenaSize, &profiler);

  /* Invoke is expected to fail (no op registered) — that is by design. */
  runner->RunSingleIteration();

  LOG_INF("=== TFLM memory footprint benchmark ===");
  LOG_INF("Framework-only binary (no kernels). Invoke error is expected.");
  LOG_INF("Tensor arena: %d bytes", kTensorArenaSize);
  LOG_INF("Model: simple_add_model (%u bytes)", g_simple_add_model_model_data_len);
  LOG_INF("Check .text+.rodata via `arm-none-eabi-size firmware.elf` for the framework footprint.");
  return 0;
}
