#include "theft4_ios_audio_output.h"

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

#include <rex/audio/conversion.h>
#include <rex/logging.h>

namespace {

constexpr uint32_t kSampleRate = 48000;
constexpr uint32_t kChannels = 2;
constexpr uint32_t kGuestFramesPerBlock = 256;
constexpr uint32_t kRingFrames = 16384;

}  // namespace

struct theft4_ios_audio_output {
  AudioUnit unit = nullptr;
  std::array<float, kRingFrames * kChannels> ring{};
  std::atomic<uint64_t> read_frame{0};
  std::atomic<uint64_t> write_frame{0};
  std::atomic<uint64_t> underrun_frames{0};
  std::atomic<uint64_t> dropped_blocks{0};
  std::atomic<uint64_t> submitted_blocks{0};
  std::mutex producer_mutex;
};

namespace {

OSStatus RenderAudio(void* context, AudioUnitRenderActionFlags*,
                     const AudioTimeStamp*, UInt32, UInt32 frame_count,
                     AudioBufferList* buffers) {
  auto* output = static_cast<theft4_ios_audio_output*>(context);
  if (!output || !buffers) return noErr;

  const uint64_t read = output->read_frame.load(std::memory_order_relaxed);
  const uint64_t write = output->write_frame.load(std::memory_order_acquire);
  const uint32_t available =
      static_cast<uint32_t>(std::min<uint64_t>(write - read, frame_count));

  if (buffers->mNumberBuffers == 1 && buffers->mBuffers[0].mData) {
    auto* destination = static_cast<float*>(buffers->mBuffers[0].mData);
    for (uint32_t frame = 0; frame < available; ++frame) {
      const size_t source = ((read + frame) % kRingFrames) * kChannels;
      destination[frame * 2] = output->ring[source];
      destination[frame * 2 + 1] = output->ring[source + 1];
    }
    std::fill(destination + available * 2,
              destination + frame_count * 2, 0.0f);
  } else if (buffers->mNumberBuffers >= 2 &&
             buffers->mBuffers[0].mData && buffers->mBuffers[1].mData) {
    auto* left = static_cast<float*>(buffers->mBuffers[0].mData);
    auto* right = static_cast<float*>(buffers->mBuffers[1].mData);
    for (uint32_t frame = 0; frame < available; ++frame) {
      const size_t source = ((read + frame) % kRingFrames) * kChannels;
      left[frame] = output->ring[source];
      right[frame] = output->ring[source + 1];
    }
    std::fill(left + available, left + frame_count, 0.0f);
    std::fill(right + available, right + frame_count, 0.0f);
  } else {
    for (UInt32 index = 0; index < buffers->mNumberBuffers; ++index) {
      if (buffers->mBuffers[index].mData) {
        std::memset(buffers->mBuffers[index].mData, 0,
                    buffers->mBuffers[index].mDataByteSize);
      }
    }
  }

  output->read_frame.store(read + available, std::memory_order_release);
  if (available < frame_count) {
    output->underrun_frames.fetch_add(frame_count - available,
                                      std::memory_order_relaxed);
  }
  return noErr;
}

bool ConfigureAudioSession() {
  AVAudioSession* session = AVAudioSession.sharedInstance;
  NSError* error = nil;
  if (![session setCategory:AVAudioSessionCategoryPlayback
                       mode:AVAudioSessionModeDefault
                    options:0
                      error:&error]) {
    REXLOG_ERROR("Theft4 audio session category failed: {}",
                 error.localizedDescription.UTF8String ?: "unknown error");
    return false;
  }
  [session setPreferredSampleRate:kSampleRate error:&error];
  error = nil;
  [session setPreferredIOBufferDuration:
               (static_cast<double>(kGuestFramesPerBlock) / kSampleRate)
                               error:&error];
  error = nil;
  if (![session setActive:YES error:&error]) {
    REXLOG_ERROR("Theft4 audio session activation failed: {}",
                 error.localizedDescription.UTF8String ?: "unknown error");
    return false;
  }
  return true;
}
}  // namespace

theft4_ios_audio_output* theft4_ios_audio_output_create() {
  if (!ConfigureAudioSession()) return nullptr;

  auto* output = new theft4_ios_audio_output();
  AudioComponentDescription description{};
  description.componentType = kAudioUnitType_Output;
  description.componentSubType = kAudioUnitSubType_RemoteIO;
  description.componentManufacturer = kAudioUnitManufacturer_Apple;
  AudioComponent component = AudioComponentFindNext(nullptr, &description);
  if (!component ||
      AudioComponentInstanceNew(component, &output->unit) != noErr) {
    REXLOG_ERROR("Theft4 AudioUnit creation failed");
    delete output;
    return nullptr;
  }

  AudioStreamBasicDescription format{};
  format.mSampleRate = kSampleRate;
  format.mFormatID = kAudioFormatLinearPCM;
  format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
  format.mBytesPerPacket = sizeof(float) * kChannels;
  format.mFramesPerPacket = 1;
  format.mBytesPerFrame = sizeof(float) * kChannels;
  format.mChannelsPerFrame = kChannels;
  format.mBitsPerChannel = sizeof(float) * 8;
  AURenderCallbackStruct callback{RenderAudio, output};

  OSStatus status = AudioUnitSetProperty(
      output->unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
      0, &format, sizeof(format));
  if (status == noErr) {
    status = AudioUnitSetProperty(
        output->unit, kAudioUnitProperty_SetRenderCallback,
        kAudioUnitScope_Input, 0, &callback, sizeof(callback));
  }
  if (status == noErr) status = AudioUnitInitialize(output->unit);
  if (status == noErr) status = AudioOutputUnitStart(output->unit);
  if (status != noErr) {
    REXLOG_ERROR("Theft4 AudioUnit initialization failed with status {}",
                 int32_t(status));
    AudioComponentInstanceDispose(output->unit);
    delete output;
    return nullptr;
  }

  REXLOG_INFO(
      "Theft4 native iOS audio output started: 48 kHz stereo, {}-frame ring",
      kRingFrames);
  return output;
}

void theft4_ios_audio_output_destroy(theft4_ios_audio_output* output) {
  if (!output) return;
  if (output->unit) {
    AudioOutputUnitStop(output->unit);
    AudioUnitUninitialize(output->unit);
    AudioComponentInstanceDispose(output->unit);
  }
  delete output;
}

bool theft4_ios_audio_output_submit(theft4_ios_audio_output* output,
                                    const float* guest_samples,
                                    size_t guest_frame_count) {
  if (!output || !guest_samples ||
      guest_frame_count != kGuestFramesPerBlock) {
    return false;
  }

  std::array<float, kGuestFramesPerBlock * kChannels> stereo{};
  rex::audio::conversion::sequential_6_BE_to_interleaved_2_LE(
      stereo.data(), guest_samples, guest_frame_count);

  std::lock_guard lock(output->producer_mutex);
  const uint64_t read = output->read_frame.load(std::memory_order_acquire);
  const uint64_t write = output->write_frame.load(std::memory_order_relaxed);
  if (write - read + guest_frame_count > kRingFrames) {
    output->dropped_blocks.fetch_add(1, std::memory_order_relaxed);
    return false;
  }
  for (size_t frame = 0; frame < guest_frame_count; ++frame) {
    const size_t destination = ((write + frame) % kRingFrames) * kChannels;
    output->ring[destination] = stereo[frame * 2];
    output->ring[destination + 1] = stereo[frame * 2 + 1];
  }
  output->write_frame.store(write + guest_frame_count,
                            std::memory_order_release);

  const uint64_t submitted =
      output->submitted_blocks.fetch_add(1, std::memory_order_relaxed) + 1;
  if (submitted <= 4 || submitted == 16 || submitted == 64 ||
      submitted == 256 || submitted == 1024) {
    float peak = 0.0f;
    for (float sample : stereo) peak = std::max(peak, std::abs(sample));
    REXLOG_INFO(
        "[Theft4Audio] blocks={} peak={:.6f} queued_frames={} underrun_frames={} dropped={}",
        submitted, peak, write + guest_frame_count - read,
        output->underrun_frames.load(std::memory_order_relaxed),
        output->dropped_blocks.load(std::memory_order_relaxed));
  }
  return true;
}
