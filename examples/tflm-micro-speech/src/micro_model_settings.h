/* SPDX-License-Identifier: Apache-2.0 */
/* Copied from tflite-micro examples/micro_speech/micro_model_settings.h */
#ifndef TENSORFLOW_LITE_MICRO_EXAMPLES_MICRO_SPEECH_MICRO_MODEL_SETTINGS_H_
#define TENSORFLOW_LITE_MICRO_EXAMPLES_MICRO_SPEECH_MICRO_MODEL_SETTINGS_H_

constexpr int kAudioSampleFrequency = 16000;
constexpr int kFeatureSize = 40;
constexpr int kFeatureCount = 49;
constexpr int kFeatureElementCount = (kFeatureSize * kFeatureCount);
constexpr int kFeatureStrideMs = 20;
constexpr int kFeatureDurationMs = 30;

constexpr int kCategoryCount = 4;
constexpr const char* kCategoryLabels[kCategoryCount] = {
    "silence", "unknown", "yes", "no",
};

#endif
