#include "audio_data.h"

#include "audio_provider.h"
#include "micro_model_settings.h"

// Your embedded audio — defined in audio_data.cc

namespace {
  int32_t g_latest_audio_timestamp = 0;
  int16_t g_audio_buffer[kMaxAudioSampleSize];
}

TfLiteStatus GetAudioSamples(int start_ms, int duration_ms,
                              int* audio_samples_size,
                              int16_t** audio_samples) {
  // Convert ms to sample index
  int start_sample = (start_ms * kAudioSampleFrequency) / 1000;
  int num_samples = kMaxAudioSampleSize;

  for (int i = 0; i < num_samples; i++) {
    int idx = start_sample + i;
    if (idx < g_embedded_audio_len) {
      g_audio_buffer[i] = g_embedded_audio[idx];
    } else {
      g_audio_buffer[i] = 0;  // pad with silence
    }
  }

  *audio_samples_size = num_samples;
  *audio_samples = g_audio_buffer;
  return kTfLiteOk;
}

int32_t LatestAudioTimestamp() {
  // Advance time by 20ms each call to drive the feature provider forward
  g_latest_audio_timestamp += 20;
  // Reset after end of audio to loop
  int total_ms = (g_embedded_audio_len * 1000) / kAudioSampleFrequency;
  if (g_latest_audio_timestamp > total_ms) {
    g_latest_audio_timestamp = 0;
  }
  return g_latest_audio_timestamp;
}
