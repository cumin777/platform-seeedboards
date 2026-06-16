/* SPDX-License-Identifier: Apache-2.0 */
/* Adapted from tflite-micro examples/micro_speech/micro_speech_test.cc.
 * Dual-model keyword-spotting pipeline (Stage A, embedded test audio):
 *   1. AudioPreprocessor int8 model: raw 16kHz PCM -> spectrogram features
 *   2. MicroSpeech quantized model: features -> silence/unknown/yes/no probs
 * Runs over embedded "yes" and "no" 1-second clips and prints predictions. */
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <algorithm>
#include <cstdint>

#include "tensorflow/lite/core/c/common.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "micro_model_settings.h"
#include "audio_preprocessor_int8_model_data.h"
#include "micro_speech_quantized_model_data.h"
#include "yes_1000ms_audio_data.h"
#include "no_1000ms_audio_data.h"

LOG_MODULE_REGISTER(micro_speech, CONFIG_LOG_DEFAULT_LEVEL);

constexpr size_t kArenaSize = 28584;
alignas(16) uint8_t g_arena[kArenaSize];

using Features = int8_t[kFeatureCount][kFeatureSize];
Features g_features;

constexpr int kAudioSampleDurationCount =
    kFeatureDurationMs * kAudioSampleFrequency / 1000;
constexpr int kAudioSampleStrideCount =
    kFeatureStrideMs * kAudioSampleFrequency / 1000;

using MicroSpeechOpResolver = tflite::MicroMutableOpResolver<4>;
using AudioPreprocessorOpResolver = tflite::MicroMutableOpResolver<18>;

static TfLiteStatus GenerateSingleFeature(const int16_t* audio_data,
                                          int8_t* feature_output,
                                          tflite::MicroInterpreter* interp) {
  TfLiteTensor* input = interp->input(0);
  TfLiteTensor* output = interp->output(0);
  std::copy_n(audio_data, kAudioSampleDurationCount,
              tflite::GetTensorData<int16_t>(input));
  TF_LITE_ENSURE_STATUS(interp->Invoke());
  std::copy_n(tflite::GetTensorData<int8_t>(output), kFeatureSize,
              feature_output);
  return kTfLiteOk;
}

static TfLiteStatus GenerateFeatures(const int16_t* audio_data,
                                     size_t audio_data_size,
                                     Features* features_output) {
  const tflite::Model* model =
      tflite::GetModel(g_audio_preprocessor_int8_model_data);
  AudioPreprocessorOpResolver op_resolver;
  op_resolver.AddReshape();
  op_resolver.AddCast();
  op_resolver.AddStridedSlice();
  op_resolver.AddConcatenation();
  op_resolver.AddMul();
  op_resolver.AddAdd();
  op_resolver.AddDiv();
  op_resolver.AddMinimum();
  op_resolver.AddMaximum();
  op_resolver.AddWindow();
  op_resolver.AddFftAutoScale();
  op_resolver.AddRfft();
  op_resolver.AddEnergy();
  op_resolver.AddFilterBank();
  op_resolver.AddFilterBankSquareRoot();
  op_resolver.AddFilterBankSpectralSubtraction();
  op_resolver.AddPCAN();
  op_resolver.AddFilterBankLog();

  tflite::MicroInterpreter interpreter(model, op_resolver, g_arena, kArenaSize);
  if (interpreter.AllocateTensors() != kTfLiteOk) {
    MicroPrintf("AudioPreprocessor AllocateTensors failed");
    return kTfLiteError;
  }
  LOG_INF("AudioPreprocessor arena used: %u B", interpreter.arena_used_bytes());

  size_t remaining = audio_data_size;
  size_t feature_index = 0;
  while (remaining >= (size_t)kAudioSampleDurationCount &&
         feature_index < (size_t)kFeatureCount) {
    TF_LITE_ENSURE_STATUS(GenerateSingleFeature(
        audio_data, (*features_output)[feature_index], &interpreter));
    feature_index++;
    audio_data += kAudioSampleStrideCount;
    remaining -= kAudioSampleStrideCount;
  }
  return kTfLiteOk;
}

static int PredictCategory(const Features& features) {
  const tflite::Model* model =
      tflite::GetModel(g_micro_speech_quantized_model_data);
  MicroSpeechOpResolver op_resolver;
  op_resolver.AddReshape();
  op_resolver.AddFullyConnected();
  op_resolver.AddDepthwiseConv2D();
  op_resolver.AddSoftmax();

  tflite::MicroInterpreter interpreter(model, op_resolver, g_arena, kArenaSize);
  if (interpreter.AllocateTensors() != kTfLiteOk) {
    MicroPrintf("MicroSpeech AllocateTensors failed");
    return -1;
  }
  LOG_INF("MicroSpeech arena used: %u B", interpreter.arena_used_bytes());

  TfLiteTensor* input = interpreter.input(0);
  TfLiteTensor* output = interpreter.output(0);
  float output_scale = output->params.scale;
  int output_zero_point = output->params.zero_point;

  std::copy_n(&features[0][0], kFeatureElementCount,
              tflite::GetTensorData<int8_t>(input));
  if (interpreter.Invoke() != kTfLiteOk) {
    MicroPrintf("MicroSpeech Invoke failed");
    return -1;
  }

  float preds[kCategoryCount];
  int best = 0;
  for (int i = 0; i < kCategoryCount; i++) {
    preds[i] = (tflite::GetTensorData<int8_t>(output)[i] - output_zero_point) *
               output_scale;
    if (preds[i] > preds[best]) best = i;
    LOG_INF("  %.4f %s", (double)preds[i], kCategoryLabels[i]);
  }
  return best;
}

static void RunSample(const char* name, const int16_t* audio,
                      size_t audio_size) {
  LOG_INF("=== micro_speech: testing '%s' (%u samples) ===", name, audio_size);
  if (GenerateFeatures(audio, audio_size, &g_features) != kTfLiteOk) {
    LOG_INF("feature generation failed");
    return;
  }
  int pred = PredictCategory(g_features);
  if (pred >= 0) {
    LOG_INF("'%s' -> predicted: %s", name, kCategoryLabels[pred]);
  }
}

int main(void) {
  LOG_INF("=== micro_speech keyword spotting (Stage A, embedded audio) ===");
  RunSample("yes", g_yes_1000ms_audio_data, g_yes_1000ms_audio_data_size);
  RunSample("no", g_no_1000ms_audio_data, g_no_1000ms_audio_data_size);
  LOG_INF("=== micro_speech done ===");
  return 0;
}
