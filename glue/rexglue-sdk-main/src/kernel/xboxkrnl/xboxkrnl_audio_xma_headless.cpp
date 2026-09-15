/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified 2026 - Deterministic no-output XMA service for embedded bring-up
 */

#include <algorithm>
#include <cstring>

#include <rex/audio/xma/context.h>
#include <rex/hook.h>
#include <rex/kernel/xboxkrnl/private.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

namespace rex::kernel::xboxkrnl {
using namespace rex::system;
using rex::audio::XMA_CONTEXT_DATA;

namespace {

struct XMA_LOOP_DATA {
  rex::be<uint32_t> loop_start;
  rex::be<uint32_t> loop_end;
  uint8_t loop_count;
  uint8_t loop_subframe_end;
  uint8_t loop_subframe_skip;
};
static_assert_size(XMA_LOOP_DATA, 12);

struct XMA_CONTEXT_INIT {
  rex::be<uint32_t> input_buffer_0_ptr;
  rex::be<uint32_t> input_buffer_0_packet_count;
  rex::be<uint32_t> input_buffer_1_ptr;
  rex::be<uint32_t> input_buffer_1_packet_count;
  rex::be<uint32_t> input_buffer_read_offset;
  rex::be<uint32_t> output_buffer_ptr;
  rex::be<uint32_t> output_buffer_block_count;
  rex::be<uint32_t> work_buffer;
  rex::be<uint32_t> subframe_decode_count;
  rex::be<uint32_t> channel_count;
  rex::be<uint32_t> sample_rate;
  XMA_LOOP_DATA loop_data;
};
static_assert_size(XMA_CONTEXT_INIT, 56);

uint32_t PhysicalAddress(uint32_t guest_address) {
  if (!guest_address) {
    return 0;
  }
  const uint32_t physical =
      REX_KERNEL_MEMORY()->GetPhysicalAddress(guest_address);
  return physical == UINT32_MAX ? 0 : physical;
}

void ProduceSilentBuffer(mapped_void context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  const uint32_t bytes = context.output_buffer_block_count * 256;
  if (context.output_buffer_ptr && bytes) {
    void* output =
        REX_KERNEL_MEMORY()->TranslatePhysical(context.output_buffer_ptr);
    if (output) {
      std::memset(output, 0, bytes);
    }
  }

  // Model one completed hardware decode batch. This intentionally produces
  // silence, but it preserves the guest-visible buffer handoff protocol so
  // the title can continue while the native XMA decoder is being integrated.
  context.input_buffer_0_valid = 0;
  context.input_buffer_1_valid = 0;
  context.output_buffer_write_offset =
      context.output_buffer_block_count
          ? std::min<uint32_t>(context.output_buffer_block_count - 1, 31)
          : 0;
  context.Store(context_ptr);
}

}  // namespace

u32 XMACreateContext_entry(mapped_u32 context_out_ptr) {
  const uint32_t context = REX_KERNEL_MEMORY()->SystemHeapAlloc(
      sizeof(XMA_CONTEXT_DATA), 256, rex::memory::kSystemHeapPhysical);
  *context_out_ptr = context;
  if (!context) {
    return X_STATUS_NO_MEMORY;
  }
  REXKRNL_NOISY_DEBUG("Headless XMA context allocated at {:08X}", context);
  return X_STATUS_SUCCESS;
}

u32 XMAReleaseContext_entry(mapped_void context_ptr) {
  REX_KERNEL_MEMORY()->SystemHeapFree(context_ptr.guest_address());
  return 0;
}

u32 XMAInitializeContext_entry(mapped_void context_ptr,
                               ppc_ptr_t<XMA_CONTEXT_INIT> context_init) {
  std::memset(context_ptr, 0, sizeof(XMA_CONTEXT_DATA));
  XMA_CONTEXT_DATA context(context_ptr);
  context.input_buffer_0_ptr = PhysicalAddress(context_init->input_buffer_0_ptr);
  context.input_buffer_0_packet_count = context_init->input_buffer_0_packet_count;
  context.input_buffer_1_ptr = PhysicalAddress(context_init->input_buffer_1_ptr);
  context.input_buffer_1_packet_count = context_init->input_buffer_1_packet_count;
  context.input_buffer_read_offset = context_init->input_buffer_read_offset;
  context.output_buffer_ptr = PhysicalAddress(context_init->output_buffer_ptr);
  context.output_buffer_block_count = context_init->output_buffer_block_count;
  context.subframe_decode_count = context_init->subframe_decode_count;
  context.is_stereo = context_init->channel_count >= 1;
  context.sample_rate = context_init->sample_rate;
  context.loop_start = context_init->loop_data.loop_start;
  context.loop_end = context_init->loop_data.loop_end;
  context.loop_count = context_init->loop_data.loop_count;
  context.loop_subframe_end = context_init->loop_data.loop_subframe_end;
  context.loop_subframe_skip = context_init->loop_data.loop_subframe_skip;
  context.Store(context_ptr);
  return 0;
}

u32 XMASetLoopData_entry(mapped_void context_ptr,
                         ppc_ptr_t<XMA_CONTEXT_DATA> loop_data) {
  XMA_CONTEXT_DATA context(context_ptr);
  context.loop_start = loop_data->loop_start;
  context.loop_end = loop_data->loop_end;
  context.loop_count = loop_data->loop_count;
  context.loop_subframe_end = loop_data->loop_subframe_end;
  context.loop_subframe_skip = loop_data->loop_subframe_skip;
  context.Store(context_ptr);
  return 0;
}

u32 XMAGetInputBufferReadOffset_entry(mapped_void context_ptr) {
  return XMA_CONTEXT_DATA(context_ptr).input_buffer_read_offset;
}

u32 XMASetInputBufferReadOffset_entry(mapped_void context_ptr, u32 value) {
  XMA_CONTEXT_DATA context(context_ptr);
  context.input_buffer_read_offset = value;
  context.Store(context_ptr);
  return 0;
}

u32 XMASetInputBuffer0_entry(mapped_void context_ptr, mapped_void buffer,
                             u32 packet_count) {
  XMA_CONTEXT_DATA context(context_ptr);
  context.input_buffer_0_ptr = PhysicalAddress(buffer.guest_address());
  context.input_buffer_0_packet_count = packet_count;
  context.Store(context_ptr);
  return 0;
}

u32 XMAIsInputBuffer0Valid_entry(mapped_void context_ptr) {
  return XMA_CONTEXT_DATA(context_ptr).input_buffer_0_valid;
}

u32 XMASetInputBuffer0Valid_entry(mapped_void context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  context.input_buffer_0_valid = 1;
  context.Store(context_ptr);
  return 0;
}

u32 XMASetInputBuffer1_entry(mapped_void context_ptr, mapped_void buffer,
                             u32 packet_count) {
  XMA_CONTEXT_DATA context(context_ptr);
  context.input_buffer_1_ptr = PhysicalAddress(buffer.guest_address());
  context.input_buffer_1_packet_count = packet_count;
  context.Store(context_ptr);
  return 0;
}

u32 XMAIsInputBuffer1Valid_entry(mapped_void context_ptr) {
  return XMA_CONTEXT_DATA(context_ptr).input_buffer_1_valid;
}

u32 XMASetInputBuffer1Valid_entry(mapped_void context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  context.input_buffer_1_valid = 1;
  context.Store(context_ptr);
  return 0;
}

u32 XMAIsOutputBufferValid_entry(mapped_void context_ptr) {
  return XMA_CONTEXT_DATA(context_ptr).output_buffer_valid;
}

u32 XMASetOutputBufferValid_entry(mapped_void context_ptr) {
  XMA_CONTEXT_DATA context(context_ptr);
  context.output_buffer_valid = 1;
  context.Store(context_ptr);
  return 0;
}

u32 XMAGetOutputBufferReadOffset_entry(mapped_void context_ptr) {
  return XMA_CONTEXT_DATA(context_ptr).output_buffer_read_offset;
}

u32 XMASetOutputBufferReadOffset_entry(mapped_void context_ptr, u32 value) {
  XMA_CONTEXT_DATA context(context_ptr);
  context.output_buffer_read_offset = value;
  if (context.output_buffer_read_offset == context.output_buffer_write_offset) {
    context.output_buffer_valid = 0;
  }
  context.Store(context_ptr);
  return 0;
}

u32 XMAGetOutputBufferWriteOffset_entry(mapped_void context_ptr) {
  return XMA_CONTEXT_DATA(context_ptr).output_buffer_write_offset;
}

u32 XMAGetPacketMetadata_entry(mapped_void context_ptr) {
  return XMA_CONTEXT_DATA(context_ptr).packet_metadata;
}

u32 XMAEnableContext_entry(mapped_void context_ptr) {
  ProduceSilentBuffer(context_ptr);
  return 0;
}

u32 XMADisableContext_entry(mapped_void, u32) { return X_E_SUCCESS; }

u32 XMABlockWhileInUse_entry(mapped_void) { return 0; }

}  // namespace rex::kernel::xboxkrnl

REX_EXPORT(__imp__XMACreateContext,
           rex::kernel::xboxkrnl::XMACreateContext_entry)
REX_EXPORT(__imp__XMAReleaseContext,
           rex::kernel::xboxkrnl::XMAReleaseContext_entry)
REX_EXPORT(__imp__XMAInitializeContext,
           rex::kernel::xboxkrnl::XMAInitializeContext_entry)
REX_EXPORT(__imp__XMASetLoopData,
           rex::kernel::xboxkrnl::XMASetLoopData_entry)
REX_EXPORT(__imp__XMAGetInputBufferReadOffset,
           rex::kernel::xboxkrnl::XMAGetInputBufferReadOffset_entry)
REX_EXPORT(__imp__XMASetInputBufferReadOffset,
           rex::kernel::xboxkrnl::XMASetInputBufferReadOffset_entry)
REX_EXPORT(__imp__XMASetInputBuffer0,
           rex::kernel::xboxkrnl::XMASetInputBuffer0_entry)
REX_EXPORT(__imp__XMAIsInputBuffer0Valid,
           rex::kernel::xboxkrnl::XMAIsInputBuffer0Valid_entry)
REX_EXPORT(__imp__XMASetInputBuffer0Valid,
           rex::kernel::xboxkrnl::XMASetInputBuffer0Valid_entry)
REX_EXPORT(__imp__XMASetInputBuffer1,
           rex::kernel::xboxkrnl::XMASetInputBuffer1_entry)
REX_EXPORT(__imp__XMAIsInputBuffer1Valid,
           rex::kernel::xboxkrnl::XMAIsInputBuffer1Valid_entry)
REX_EXPORT(__imp__XMASetInputBuffer1Valid,
           rex::kernel::xboxkrnl::XMASetInputBuffer1Valid_entry)
REX_EXPORT(__imp__XMAIsOutputBufferValid,
           rex::kernel::xboxkrnl::XMAIsOutputBufferValid_entry)
REX_EXPORT(__imp__XMASetOutputBufferValid,
           rex::kernel::xboxkrnl::XMASetOutputBufferValid_entry)
REX_EXPORT(__imp__XMAGetOutputBufferReadOffset,
           rex::kernel::xboxkrnl::XMAGetOutputBufferReadOffset_entry)
REX_EXPORT(__imp__XMASetOutputBufferReadOffset,
           rex::kernel::xboxkrnl::XMASetOutputBufferReadOffset_entry)
REX_EXPORT(__imp__XMAGetOutputBufferWriteOffset,
           rex::kernel::xboxkrnl::XMAGetOutputBufferWriteOffset_entry)
REX_EXPORT(__imp__XMAGetPacketMetadata,
           rex::kernel::xboxkrnl::XMAGetPacketMetadata_entry)
REX_EXPORT(__imp__XMAEnableContext,
           rex::kernel::xboxkrnl::XMAEnableContext_entry)
REX_EXPORT(__imp__XMADisableContext,
           rex::kernel::xboxkrnl::XMADisableContext_entry)
REX_EXPORT(__imp__XMABlockWhileInUse,
           rex::kernel::xboxkrnl::XMABlockWhileInUse_entry)
