#pragma once

#include <cstddef>

struct theft4_ios_audio_output;

// Creates a 48 kHz stereo iOS playback path backed by a real-time-safe ring.
// Submitted blocks contain the Xbox mixer's six sequential, big-endian float
// channels (256 frames each).
theft4_ios_audio_output* theft4_ios_audio_output_create();
void theft4_ios_audio_output_destroy(theft4_ios_audio_output* output);
bool theft4_ios_audio_output_submit(theft4_ios_audio_output* output,
                                    const float* guest_samples,
                                    size_t guest_frame_count);
