/* SPDX-License-Identifier: Apache-2.0 */
/* Adapted from tflite-micro examples/person_detection (Stage A, embedded test
 * images). Runs a 96x96 int8 Visual Wake Words CNN over embedded person and
 * no_person images and prints the person/no-person scores. No camera needed. */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <cstdint>

#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/kernels/conv.h"
#include "tensorflow/lite/micro/kernels/depthwise_conv.h"
#include "tensorflow/lite/micro/kernels/pooling.h"
#include "tensorflow/lite/micro/kernels/softmax.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "person_detect_model_data.h"
#include "person_image_data.h"
#include "no_person_image_data.h"

LOG_MODULE_REGISTER(person_detection, CONFIG_LOG_DEFAULT_LEVEL);

constexpr int kTensorArenaSize = 136 * 1024;
alignas(16) uint8_t tensor_arena[kTensorArenaSize];

constexpr int kNumCols = 96;
constexpr int kNumRows = 96;
constexpr int kNumChannels = 1;
constexpr int kMaxImageSize = kNumCols * kNumRows * kNumChannels;

static int RunImage(tflite::MicroInterpreter* interpreter,
                    const char* name, const int8_t* image_data,
                    unsigned int image_size) {
  TfLiteTensor* input = interpreter->input(0);
  if (image_size < (unsigned)kMaxImageSize) {
    MicroPrintf("image too small: %u", image_size);
    return -1;
  }
  memcpy(tflite::GetTensorData<int8_t>(input), image_data, kMaxImageSize);

  if (interpreter->Invoke() != kTfLiteOk) {
    MicroPrintf("Invoke failed on %s", name);
    return -1;
  }

  TfLiteTensor* output = interpreter->output(0);
  int8_t no_person_score = tflite::GetTensorData<int8_t>(output)[0];
  int8_t person_score = tflite::GetTensorData<int8_t>(output)[1];
  int pred = (person_score > no_person_score) ? 1 : 0;
  LOG_INF("%s: person=%d no_person=%d -> %s", name, person_score,
          no_person_score, pred ? "PERSON" : "NO PERSON");
  return pred;
}

int main(void) {
  const tflite::Model* model = tflite::GetModel(g_person_detect_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("Model schema version %d != supported %d",
                model->version(), TFLITE_SCHEMA_VERSION);
    return 1;
  }

  tflite::MicroMutableOpResolver<5> resolver;
  resolver.AddDepthwiseConv2D(tflite::Register_DEPTHWISE_CONV_2D_INT8());
  resolver.AddConv2D(tflite::Register_CONV_2D_INT8());
  resolver.AddAveragePool2D(tflite::Register_AVERAGE_POOL_2D_INT8());
  resolver.AddReshape();
  resolver.AddSoftmax(tflite::Register_SOFTMAX_INT8());

  tflite::MicroInterpreter interpreter(model, resolver, tensor_arena,
                                       kTensorArenaSize);
  if (interpreter.AllocateTensors() != kTfLiteOk) {
    MicroPrintf("AllocateTensors failed");
    return 1;
  }
  LOG_INF("=== person detection (Stage A, embedded images) ===");
  LOG_INF("arena used: %u B", interpreter.arena_used_bytes());

  RunImage(&interpreter, "person.bmp", g_person_image_data,
           g_person_image_data_size);
  RunImage(&interpreter, "no_person.bmp", g_no_person_image_data,
           g_no_person_image_data_size);
  LOG_INF("=== person detection done ===");
  return 0;
}
