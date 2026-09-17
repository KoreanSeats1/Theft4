/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <cstdint>

#include <rex/assert.h>
#include <rex/diagnostics/gpu_flight_recorder.h>
#include <rex/logging.h>
#include <rex/ui/vulkan/submission_tracker.h>
#include <rex/ui/vulkan/util.h>

namespace rex {
namespace ui {
namespace vulkan {

namespace gpu_flight = rex::diagnostics::gpu_flight;

VulkanSubmissionTracker::FenceAcquisition::~FenceAcquisition() {
  if (!submission_tracker_) {
    // Dropped submission or left after std::move.
    return;
  }
  assert_true(submission_tracker_->fence_acquired_ == fence_);
  gpu_flight::Record("fence.submission-register", uint64_t(uintptr_t(fence_)),
                     submission_tracker_->submission_current_, 0,
                     uint64_t(uintptr_t(submission_tracker_)), signal_failed_);
  if (fence_ != VK_NULL_HANDLE) {
    if (signal_failed_) {
      // Left in the unsignaled state.
      submission_tracker_->fences_reclaimed_.push_back(fence_);
    } else {
      // Left in the pending state.
      submission_tracker_->fences_pending_.emplace_back(submission_tracker_->submission_current_,
                                                        fence_);
    }
    submission_tracker_->fence_acquired_ = VK_NULL_HANDLE;
  }
  ++submission_tracker_->submission_current_;
}

void VulkanSubmissionTracker::Shutdown() {
  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();
  if (!AwaitAllSubmissionsCompletion()) {
    const VkResult idle_result = dfn.vkDeviceWaitIdle(device);
    gpu_flight::Record("tracker.device-idle", uint64_t(uintptr_t(device)), submission_current_,
                       0, uint64_t(uintptr_t(this)), 0, int32_t(idle_result));
    if (idle_result < VK_SUCCESS) {
      gpu_flight::Fail("tracker.device-idle", int32_t(idle_result),
                       uint64_t(uintptr_t(device)), submission_current_);
    }
    if (idle_result != VK_SUCCESS && idle_result != VK_ERROR_DEVICE_LOST) {
      // Destroying pending fences is invalid. Leave them to vkDestroyDevice,
      // which is the enclosing lifetime owner during fatal shutdown.
      REXLOG_ERROR(
          "VulkanSubmissionTracker: cannot prove queue completion result={}; retaining {} "
          "pending fences for device teardown",
          int32_t(idle_result), fences_pending_.size());
      fences_reclaimed_.clear();
      fences_pending_.clear();
      fence_acquired_ = VK_NULL_HANDLE;
      return;
    }
  }
  for (VkFence fence : fences_reclaimed_) {
    dfn.vkDestroyFence(device, fence, nullptr);
  }
  fences_reclaimed_.clear();
  for (const std::pair<uint64_t, VkFence>& fence_pair : fences_pending_) {
    dfn.vkDestroyFence(device, fence_pair.second, nullptr);
  }
  fences_pending_.clear();
  assert_true(fence_acquired_ == VK_NULL_HANDLE);
  util::DestroyAndNullHandle(dfn.vkDestroyFence, device, fence_acquired_);
}

void VulkanSubmissionTracker::FenceAcquisition::SubmissionFailedOrDropped() {
  if (!submission_tracker_) {
    return;
  }
  assert_true(submission_tracker_->fence_acquired_ == fence_);
  gpu_flight::Record("fence.submission-dropped", uint64_t(uintptr_t(fence_)),
                     submission_tracker_->submission_current_, 0,
                     uint64_t(uintptr_t(submission_tracker_)));
  if (fence_ != VK_NULL_HANDLE) {
    submission_tracker_->fences_reclaimed_.push_back(fence_);
  }
  submission_tracker_->fence_acquired_ = VK_NULL_HANDLE;
  fence_ = VK_NULL_HANDLE;
  // No submission acquisition from now on, don't increment the current
  // submission index as well.
  submission_tracker_ = VK_NULL_HANDLE;
}

uint64_t VulkanSubmissionTracker::UpdateAndGetCompletedSubmission() {
  if (!fences_pending_.empty()) {
    const VulkanDevice::Functions& dfn = vulkan_device_->functions();
    const VkDevice device = vulkan_device_->device();
    while (!fences_pending_.empty()) {
      const std::pair<uint64_t, VkFence>& pending_pair = fences_pending_.front();
      assert_true(pending_pair.first > submission_completed_on_gpu_);
      const VkResult poll_result = dfn.vkGetFenceStatus(device, pending_pair.second);
      if (poll_result < VK_SUCCESS) {
        gpu_flight::Fail("fence.poll", int32_t(poll_result),
                         uint64_t(uintptr_t(pending_pair.second)), pending_pair.first);
      }
      if (poll_result != VK_SUCCESS) {
        break;
      }
      gpu_flight::Record("fence.completed", uint64_t(uintptr_t(pending_pair.second)),
                         pending_pair.first, 0, uint64_t(uintptr_t(this)));
      fences_reclaimed_.push_back(pending_pair.second);
      submission_completed_on_gpu_ = pending_pair.first;
      fences_pending_.pop_front();
    }
  }
  return submission_completed_on_gpu_;
}

bool VulkanSubmissionTracker::AwaitSubmissionCompletion(uint64_t submission_index) {
  return AwaitSubmissionCompletion(submission_index, UINT64_MAX);
}

bool VulkanSubmissionTracker::AwaitSubmissionCompletion(uint64_t submission_index,
                                                        uint64_t timeout_nanoseconds) {
  // The tracker itself can't give a submission index for a submission that
  // hasn't even started being recorded yet, the client has provided a
  // completely invalid value or has done overly optimistic math if such an
  // index has been obtained somehow.
  assert_true(submission_index <= submission_current_);
  if (SubmissionCompletionReached(submission_completed_on_gpu_, submission_index)) {
    return true;
  }
  // Waiting for the current submission is fine if there was a failure or a
  // refusal to submit, and the submission index wasn't incremented, but still
  // need to release objects referenced in the dropped submission (while
  // shutting down, for instance - in this case, waiting for the last successful
  // submission, which could have also referenced the objects from the new
  // submission - we can't know since the client has already overwritten its
  // last usage index, would correctly ensure that GPU usage of the objects is
  // not pending). Waiting for successful submissions, but failed signals, will
  // result in a true race condition, however, but waiting for the closest
  // successful signal is the best approximation - also retrying to signal in
  // this case.
  // Go from the most recent to wait only for one fence, which includes all the
  // preceding ones.
  // "Fence signal operations that are defined by vkQueueSubmit additionally
  // include in the first synchronization scope all commands that occur earlier
  // in submission order."
  size_t reclaim_end = fences_pending_.size();
  if (reclaim_end) {
    const VulkanDevice::Functions& dfn = vulkan_device_->functions();
    const VkDevice device = vulkan_device_->device();
    while (reclaim_end) {
      const std::pair<uint64_t, VkFence>& pending_pair = fences_pending_[reclaim_end - 1];
      assert_true(pending_pair.first > submission_completed_on_gpu_);
      if (pending_pair.first <= submission_index) {
        // Wait if requested.
        gpu_flight::Record("fence.wait-begin", uint64_t(uintptr_t(pending_pair.second)),
                           pending_pair.first, 0, uint64_t(uintptr_t(this)), submission_index);
        const VkResult wait_result = dfn.vkWaitForFences(
            device, 1, &pending_pair.second, VK_TRUE, timeout_nanoseconds);
        gpu_flight::Record("fence.wait-end", uint64_t(uintptr_t(pending_pair.second)),
                           pending_pair.first, 0, uint64_t(uintptr_t(this)), submission_index,
                           int32_t(wait_result));
        if (wait_result == VK_TIMEOUT) {
          // A positive Vulkan timeout isn't an API error, but it is a terminal
          // diagnostic event for callers that explicitly requested a bounded
          // wait. Freeze and dump the preceding resource/submission history.
          gpu_flight::Fail("fence.wait-timeout", int32_t(wait_result),
                           uint64_t(uintptr_t(pending_pair.second)), pending_pair.first,
                           submission_index);
        } else if (wait_result < VK_SUCCESS) {
          gpu_flight::Fail("fence.wait", int32_t(wait_result),
                           uint64_t(uintptr_t(pending_pair.second)), pending_pair.first);
        }
        if (wait_result == VK_SUCCESS) {
          break;
        }
      }
      // Just refresh the completed submission.
      const VkResult poll_result = dfn.vkGetFenceStatus(device, pending_pair.second);
      if (poll_result < VK_SUCCESS) {
        gpu_flight::Fail("fence.wait-poll", int32_t(poll_result),
                         uint64_t(uintptr_t(pending_pair.second)), pending_pair.first);
      }
      if (poll_result == VK_SUCCESS) {
        break;
      }
      --reclaim_end;
    }
    if (reclaim_end) {
      submission_completed_on_gpu_ = fences_pending_[reclaim_end - 1].first;
      for (; reclaim_end; --reclaim_end) {
        fences_reclaimed_.push_back(fences_pending_.front().second);
        fences_pending_.pop_front();
      }
    }
  }
  return SubmissionCompletionReached(submission_completed_on_gpu_, submission_index);
}

VulkanSubmissionTracker::FenceAcquisition
VulkanSubmissionTracker::AcquireFenceToAdvanceSubmission() {
  assert_true(fence_acquired_ == VK_NULL_HANDLE);
  // Reclaim fences if the client only gets the completed submission index or
  // awaits in special cases such as shutdown.
  UpdateAndGetCompletedSubmission();
  const VulkanDevice::Functions& dfn = vulkan_device_->functions();
  const VkDevice device = vulkan_device_->device();
  if (!fences_reclaimed_.empty()) {
    VkFence reclaimed_fence = fences_reclaimed_.back();
    const VkResult reset_result = dfn.vkResetFences(device, 1, &reclaimed_fence);
    gpu_flight::Record("fence.reset", uint64_t(uintptr_t(reclaimed_fence)), submission_current_,
                       0, uint64_t(uintptr_t(this)), submission_completed_on_gpu_,
                       int32_t(reset_result));
    if (reset_result < VK_SUCCESS) {
      gpu_flight::Fail("fence.reset", int32_t(reset_result),
                       uint64_t(uintptr_t(reclaimed_fence)), submission_current_);
    }
    if (reset_result == VK_SUCCESS) {
      fence_acquired_ = fences_reclaimed_.back();
      fences_reclaimed_.pop_back();
    }
  }
  if (fence_acquired_ == VK_NULL_HANDLE) {
    VkFenceCreateInfo fence_create_info;
    fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence_create_info.pNext = nullptr;
    fence_create_info.flags = 0;
    // May fail, a null fence is handled in FenceAcquisition.
    const VkResult create_result =
        dfn.vkCreateFence(device, &fence_create_info, nullptr, &fence_acquired_);
    gpu_flight::Record("fence.create", uint64_t(uintptr_t(fence_acquired_)), submission_current_,
                       0, uint64_t(uintptr_t(this)), 0, int32_t(create_result));
    if (create_result < VK_SUCCESS) {
      gpu_flight::Fail("fence.create", int32_t(create_result),
                       uint64_t(uintptr_t(device)), submission_current_);
    }
  }
  return FenceAcquisition(*this, fence_acquired_);
}

}  // namespace vulkan
}  // namespace ui
}  // namespace rex
