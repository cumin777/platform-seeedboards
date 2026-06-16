/* SPDX-License-Identifier: Apache-2.0 */
/* Adapted from tflite-micro examples/mnist_lstm. Runs an int8 LSTM model over
 * 10 embedded MNIST digit samples and prints the predicted digit for each.
 * Model input: (1,28,28) int8, scale=1/255, zp=-128  => int8 = pixel - 128.
 * Model output: (1,10) int8 softmax; argmax gives the predicted digit. */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_utils.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "mnist_lstm_model_data.h"
#include "mnist_digits.h"

LOG_MODULE_REGISTER(mnist_lstm, CONFIG_LOG_DEFAULT_LEVEL);

constexpr int kTensorArenaSize = 60 * 1024;
alignas(16) uint8_t tensor_arena[kTensorArenaSize];

static int ArgMax(const int8_t* data, int len) {
  int best = 0;
  for (int i = 1; i < len; ++i) {
    if (data[i] > data[best]) best = i;
  }
  return best;
}

int main(void) {
  const tflite::Model* model = tflite::GetModel(g_mnist_lstm_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("Model schema version %d != supported %d",
                model->version(), TFLITE_SCHEMA_VERSION);
    return 1;
  }

  tflite::MicroMutableOpResolver<4> resolver;
  resolver.AddUnidirectionalSequenceLSTM();
  resolver.AddReshape();
  resolver.AddFullyConnected();
  resolver.AddSoftmax();

  tflite::MicroInterpreter interpreter(model, resolver, tensor_arena,
                                       kTensorArenaSize);
  if (interpreter.AllocateTensors() != kTfLiteOk) {
    MicroPrintf("AllocateTensors failed");
    return 1;
  }

  TfLiteTensor* input = interpreter.input(0);
  TfLiteTensor* output = interpreter.output(0);
  const int out_len = tflite::ElementCount(*(output->dims));

  LOG_INF("=== MNIST LSTM digit recognition (%d samples) ===", kNumMnistSamples);

  int correct = 0;
  for (int n = 0; n < kNumMnistSamples; ++n) {
    memcpy(tflite::GetTensorData<int8_t>(input), g_mnist_samples[n],
           kMnistImageSize);

    if (interpreter.Invoke() != kTfLiteOk) {
      MicroPrintf("Invoke failed on sample %d", n);
      return 1;
    }

    int pred = ArgMax(tflite::GetTensorData<int8_t>(output), out_len);
    bool ok = (pred == n);
    correct += ok ? 1 : 0;
    LOG_INF("sample%d: predicted=%d %s", n, pred, ok ? "OK" : "(mismatch)");

    interpreter.Reset();  // LSTM is stateful — reset between independent images
  }

  LOG_INF("=== MNIST LSTM done: %d/%d correct ===", correct, kNumMnistSamples);
  return 0;
}
