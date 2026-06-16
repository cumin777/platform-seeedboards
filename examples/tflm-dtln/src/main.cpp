/* SPDX-License-Identifier: Apache-2.0 */
/* Adapted from tflite-micro examples/dtln/dtln_test.cc.
 * DTLN speech noise suppression (functional verification only — per upstream
 * this model is a HiFi-DSP LSTM demo, not for noise-quality evaluation).
 * Runs one inference frame over an embedded noisy spectrogram and compares to
 * the golden reference. GCC 14.2.1 + CMSIS-NN (s8 LSTM). */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "dtln_noise_suppression_model_data.h"
#include "dtln_inout_data.h"

LOG_MODULE_REGISTER(dtln, CONFIG_LOG_DEFAULT_LEVEL);

constexpr int kTensorArenaSize = 32 * 1024;
alignas(16) uint8_t tensor_arena[kTensorArenaSize];

int main(void) {
  const tflite::Model* model =
      tflite::GetModel(g_dtln_noise_suppression_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("Model schema version %d != supported %d",
                model->version(), TFLITE_SCHEMA_VERSION);
    return 1;
  }

  tflite::MicroMutableOpResolver<4> resolver;
  resolver.AddUnidirectionalSequenceLSTM();
  resolver.AddFullyConnected();
  resolver.AddLogistic();
  resolver.AddDiv();

  tflite::MicroInterpreter interpreter(model, resolver, tensor_arena,
                                       kTensorArenaSize);
  if (interpreter.AllocateTensors() != kTfLiteOk) {
    MicroPrintf("AllocateTensors failed");
    return 1;
  }

  TfLiteTensor* input = interpreter.input(0);
  for (size_t i = 0; i < input->bytes; ++i) {
    input->data.int8[i] = feature_data[i];
  }

  if (interpreter.Invoke() != kTfLiteOk) {
    MicroPrintf("Invoke failed");
    return 1;
  }

  TfLiteTensor* output = interpreter.output(0);
  int output_size = output->dims->data[0] * output->dims->data[1] *
                    output->dims->data[2];
  int mismatches = 0;
  for (int i = 0; i < output_size; i++) {
    if (output->data.int8[i] != golden_ref[i]) mismatches++;
  }
  LOG_INF("=== DTLN noise suppression (functional verification) ===");
  LOG_INF("arena used: %u B, output frames: %d", interpreter.arena_used_bytes(),
          output_size);
  if (mismatches == 0) {
    LOG_INF("DTLN: output matches golden reference (~~~ALL TESTS PASSED~~~)");
  } else {
    LOG_INF("DTLN: %d/%d output bytes differ from golden ref", mismatches,
            output_size);
  }
  return 0;
}
