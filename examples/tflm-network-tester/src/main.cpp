/* SPDX-License-Identifier: Apache-2.0 */
/* Adapted from tflite-micro examples/network_tester/network_tester_test.cc
 * Runs a single DepthwiseConv2D int8 model and compares against expected output.
 * Prints PASS/FAIL and a few output bytes over the Zephyr log. */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_utils.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/micro/kernels/conv.h"
#include "tensorflow/lite/micro/kernels/depthwise_conv.h"
#include "tensorflow/lite/micro/kernels/pooling.h"
#include "tensorflow/lite/micro/kernels/softmax.h"

#include "network_model.h"
#include "input_data.h"
#include "expected_output_data.h"

LOG_MODULE_REGISTER(network_tester, CONFIG_LOG_DEFAULT_LEVEL);

#ifndef TENSOR_ARENA_SIZE
#define TENSOR_ARENA_SIZE (5 * 1024)
#endif
alignas(16) uint8_t tensor_arena[TENSOR_ARENA_SIZE];

int main(void) {
  const tflite::Model* model = tflite::GetModel(network_model);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("Model schema version %d != supported %d",
                model->version(), TFLITE_SCHEMA_VERSION);
    return 1;
  }

  tflite::MicroMutableOpResolver<5> resolver;
  resolver.AddAveragePool2D(tflite::Register_AVERAGE_POOL_2D_INT8());
  resolver.AddConv2D(tflite::Register_CONV_2D_INT8());
  resolver.AddDepthwiseConv2D(tflite::Register_DEPTHWISE_CONV_2D_INT8());
  resolver.AddReshape();
  resolver.AddSoftmax(tflite::Register_SOFTMAX_INT8());

  tflite::MicroInterpreter interpreter(model, resolver, tensor_arena,
                                       TENSOR_ARENA_SIZE);
  if (interpreter.AllocateTensors() != kTfLiteOk) {
    MicroPrintf("AllocateTensors failed");
    return 1;
  }

  for (size_t i = 0; i < (size_t)interpreter.inputs_size(); ++i) {
    TfLiteTensor* input = interpreter.input(i);
    memcpy(input->data.data, &input_data[i], input->bytes);
  }

  if (interpreter.Invoke() != kTfLiteOk) {
    MicroPrintf("Invoke failed");
    return 1;
  }

  /* Compare against expected output. */
  int mismatches = 0;
  for (size_t i = 0; i < (size_t)interpreter.outputs_size(); i++) {
    TfLiteTensor* output = interpreter.output(i);
    int count = tflite::ElementCount(*(output->dims));
    for (int j = 0; j < count; ++j) {
      int8_t got = tflite::GetTensorData<int8_t>(output)[j];
      int8_t want = (int8_t)expected_output_data[j];
      if (got != want) mismatches++;
    }
    LOG_INF("output[%zu] first 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
            i,
            (uint8_t)tflite::GetTensorData<int8_t>(output)[0],
            (uint8_t)tflite::GetTensorData<int8_t>(output)[1],
            (uint8_t)tflite::GetTensorData<int8_t>(output)[2],
            (uint8_t)tflite::GetTensorData<int8_t>(output)[3],
            (uint8_t)tflite::GetTensorData<int8_t>(output)[4],
            (uint8_t)tflite::GetTensorData<int8_t>(output)[5],
            (uint8_t)tflite::GetTensorData<int8_t>(output)[6],
            (uint8_t)tflite::GetTensorData<int8_t>(output)[7]);
  }

  if (mismatches == 0) {
    LOG_INF("~~~NETWORK TESTER: ALL TESTS PASSED~~~");
  } else {
    LOG_INF("NETWORK TESTER: %d mismatches vs expected output", mismatches);
  }
  return 0;
}
