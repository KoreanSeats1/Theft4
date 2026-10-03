#include "native_masked_constants.h"
#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>

using namespace rex::graphics::gta4_native;
static void Check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
static void CopyWords(uint8_t* out, const uint8_t* in, size_t size) {
  Check(size % 4 == 0, "unaligned copy");
  for (size_t i = 0; i < size; i += 4) {
    uint32_t word; std::memcpy(&word, in + i, 4);
    word = __builtin_bswap32(word); std::memcpy(out + i, &word, 4);
  }
}
static uint64_t Hash(std::span<const uint8_t> bytes) {
  uint64_t h = 0;
  for (auto x : bytes) h = h * 131 + x;
  return h;
}
static NativeConstantMask Mask(size_t first, size_t count) {
  NativeConstantMask mask{};
  for (size_t i = first; i < first + count; ++i) mask[i / 64] |= uint64_t{1} << (i % 64);
  return mask;
}

static void RandomReplay() {
  std::mt19937 rng(0x95);
  for (size_t bank : {size_t(4096), size_t(3584)}) {
    AuthoritativeConstantState state(bank);
    std::vector<uint8_t> canonical(bank), previous_host(bank, 0xD3);
    for (auto& b : canonical) b = uint8_t(rng());
    ConstantPayloadDelta initial; Check(CaptureCompleteConstantSnapshot(canonical, initial), "initial capture");
    auto applied = state.Apply(initial, Hash); Check(bool(applied), "initial apply");
    std::shared_ptr<const ConstantStateVersion> previous;
    NativeConstantMask previous_mask{};
    for (size_t step = 0; step < 4000; ++step) {
      NativeConstantMask mask = step % 7 ? previous_mask : NativeConstantMask{};
      if (mask == NativeConstantMask{})
        for (size_t i = 0; i < 12; ++i) {
          const size_t reg = rng() % (bank / 16); mask[reg / 64] |= uint64_t{1} << (reg % 64);
        }
      const size_t offset = (rng() % (bank / 4)) * 4;
      const size_t count = std::min(size_t(4 * (1 + rng() % 8)), bank - offset);
      ConstantPayloadDelta delta; delta.ranges.push_back({uint32_t(offset), 0, uint32_t(count)});
      delta.payload.resize(count);
      for (size_t i = 0; i < count; ++i) delta.payload[i] = canonical[offset + i] = uint8_t(rng());
      applied = state.Apply(delta, Hash); Check(bool(applied), "delta apply");
      std::vector<uint8_t> result(bank, 0xD3);
      NativeMaskedConstantPlan plan;
      const bool same_mask = mask == previous_mask;
      if (plan.Build(applied.version.get(), mask, same_mask ? previous.get() : nullptr,
                     same_mask ? std::span<const uint8_t>(previous_host) : std::span<const uint8_t>{})) {
        Check(plan.Write(result, CopyWords) > 0, "plan write");
        for (size_t reg = 0; reg < bank / 16; ++reg) {
          if (mask[reg / 64] & (uint64_t{1} << (reg % 64))) {
            std::array<uint8_t,16> expected{}; CopyWords(expected.data(), canonical.data() + reg * 16, 16);
            Check(std::memcmp(expected.data(), result.data() + reg * 16, 16) == 0, "covered bytes differ");
          } else for (size_t i = 0; i < 16; ++i)
            Check(result[reg * 16 + i] == 0xD3, "unread bytes were copied");
        }
      } else {
        const auto* full = AuthoritativeConstantState::MaterializeView(applied.version);
        Check(full && *full == canonical, "complete fallback differs");
        CopyWords(result.data(), full->data(), bank);
      }
      previous = applied.version; previous_mask = mask; previous_host = std::move(result);
      // Exercise a pre-existing materialized base and parent severance too.
      if (step % 29 == 0) Check(*AuthoritativeConstantState::MaterializeView(previous) == canonical,
                              "materialization differs");
    }
  }
}

static void RejectionAndBounds() {
  auto base = std::make_shared<ConstantStateVersion>(); base->byte_size = 4096;
  base->delta.complete_snapshot = true; base->delta.ranges = {{0,0,4096}};
  base->delta.payload.assign(4096, 0x6A);
  NativeMaskedConstantPlan plan;
  Check(!plan.Build(nullptr, Mask(0, 1)), "null accepted");
  Check(!plan.Build(base.get(), {}), "empty coverage accepted");
  Check(plan.Build(base.get(), Mask(255, 1)) && plan.extent() == 4096, "last register missed");
  auto node = std::make_shared<ConstantStateVersion>(); node->byte_size = 4096; node->parent = base;
  node->delta.ranges = {{3,0,4}}; node->delta.payload.assign(4, 0x11);
  Check(!plan.Build(node.get(), Mask(0,1)), "unaligned delta accepted");
  node->delta.ranges = {{4096,0,4}};
  Check(!plan.Build(node.get(), Mask(0,1)), "out of bounds delta accepted");
  std::shared_ptr<const ConstantStateVersion> chain = base;
  for (size_t i = 0; i < 9; ++i) {
    auto next = std::make_shared<ConstantStateVersion>(); next->byte_size = 4096; next->parent = chain;
    next->delta.ranges = {{0,0,16}}; next->delta.payload.assign(16,uint8_t(i)); chain = next;
  }
  Check(!plan.Build(chain.get(), Mask(0,1)), "long replay was not bounded");
  base->byte_size = 3584;
  Check(!plan.Build(base.get(), Mask(224,1)), "pixel register bound exceeded");
}

static void Benchmark() {
  constexpr size_t draws = 5000, bank = 4096;
  AuthoritativeConstantState state(bank);
  std::vector<uint8_t> canonical(bank, 0x2A);
  ConstantPayloadDelta initial; CaptureCompleteConstantSnapshot(canonical, initial);
  state.Apply(initial, Hash);
  std::vector<std::shared_ptr<const ConstantStateVersion>> versions;
  for (size_t i = 0; i < draws; ++i) {
    ConstantPayloadDelta delta; delta.ranges = {{uint32_t((i % 32) * 16),0,16}};
    delta.payload.assign(16,uint8_t(i)); versions.push_back(state.Apply(delta,Hash).version);
  }
  std::vector<uint8_t> output(draws * bank);
  const auto mask = Mask(0,32);
  const auto baseline = [&] {
    uint64_t bytes = 0;
    for (size_t i = 0; i < draws; ++i) {
      auto* out = output.data() + i * bank; const auto& version = versions[i];
      if (i && version->parent == versions[i-1] && !version->delta.complete_snapshot) {
        std::memcpy(out, output.data() + (i-1)*bank, bank); bytes += bank;
        for (const auto& r : version->delta.ranges) {
          CopyWords(out+r.destination_offset,version->delta.payload.data()+r.payload_offset,r.byte_count);
          bytes += r.byte_count;
        }
      } else {
        CopyWords(out,state.MaterializeView(version)->data(),bank); bytes += bank;
      }
    }
    return bytes;
  };
  // Run sparse first: baseline materialization must not prewarm the new path.
  const auto sparse = [&] {
    uint64_t bytes = 0;
    for (size_t i = 0; i < draws; ++i) {
      NativeMaskedConstantPlan plan;
      Check(plan.Build(versions[i].get(),mask,i ? versions[i-1].get() : nullptr,
          i ? std::span<const uint8_t>(output.data() + (i-1)*bank, bank) : std::span<const uint8_t>{}),
          "benchmark plan failed");
      bytes += plan.Write(std::span<uint8_t>(output.data()+i*bank, bank), CopyWords);
    }
    return bytes;
  };
  auto measure = [&](auto&& run) {
    std::array<double,9> times{}; uint64_t bytes = 0;
    for (auto& time : times) {
      const auto start = std::chrono::steady_clock::now(); bytes = run();
      time = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    }
    std::sort(times.begin(),times.end()); return std::pair{times[4],bytes};
  };
  auto covered = measure(sparse); auto full = measure(baseline);
  std::cout << "BENCH 5000 changed banks, 32/256 read registers: full_ms=" << full.first
            << " covered_ms=" << covered.first << " full_bytes=" << full.second
            << " covered_bytes=" << covered.second << '\n';
}

int main(int argc, char**) {
  RandomReplay(); RejectionAndBounds();
  std::cout << "PASS: 8000 randomized state/coverage replays, materialization, unread holes, boundaries and bounded fallback\n";
  if (argc > 1) Benchmark();
}
