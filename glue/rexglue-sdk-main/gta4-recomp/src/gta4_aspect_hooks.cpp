#include "gta4_aspect_hooks.h"

#include <atomic>
#include <bit>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/diagnostics/policy.h>
#include <rex/runtime.h>
#include "gta4_init.h"

REXCVAR_DEFINE_BOOL(gta4_trace_aspect, false, "GTA IV/Diagnostics",
                    "Bounded camera, UI-layout, and fixed-artwork aspect observations");

namespace gta4::aspect {
namespace {
struct OutputState {
  Extent render, output;
  bool ready = false;
  uint64_t generation = 0;
  Extent display;
  SafeInsets safe;
};
std::mutex output_mutex;
OutputState output_state;
std::mutex radar_mutex;
Rect radar_screen_bounds{};
uint64_t radar_bounds_generation = UINT64_MAX;
thread_local UiContext ui_context;
thread_local unsigned baked_font_depth = 0;
thread_local unsigned append_depth = 0;
thread_local Point append_origin;
thread_local Transform append_transform;
thread_local uint32_t phone_projection_build = 0;
struct Emission {
  Transform transform;
  bool active = false;
  bool clip_art = false;
  Rect art_quad{}, art_clip{};
};
thread_local Emission emission;
std::atomic<uint32_t> trace_count{0};
std::atomic<uint32_t> loading_label_trace_count{0};
std::array<std::atomic<uint32_t>, 4> camera_consumer_trace_count{};
std::array<std::atomic<uint64_t>, 4> camera_consumer_trace_shape{};

bool Span(uint8_t* base, uint32_t address, size_t size, bool write = false) {
  if (!base || !address || !size || uint64_t(address) + size > uint64_t(UINT32_MAX) + 1)
    return false;
  auto* kernel = REX_KERNEL_STATE();
  auto* memory = kernel ? kernel->memory() : nullptr;
  auto* heap = memory ? memory->LookupHeap(address) : nullptr;
  const uint32_t last = uint32_t(uint64_t(address) + size - 1);
  if (!heap || heap != memory->LookupHeap(last))
    return false;
  const auto access = heap->QueryRangeAccess(address, last);
  using rex::memory::PageAccess;
  return access == PageAccess::kReadWrite || access == PageAccess::kExecuteReadWrite ||
         (!write && (access == PageAccess::kReadOnly || access == PageAccess::kExecuteReadOnly));
}
uint32_t Read(uint8_t* base, uint32_t address) {
  uint32_t bits;
  std::memcpy(&bits, rex::memory::GuestPtr(base, address), sizeof(bits));
  return __builtin_bswap32(bits);
}
void Write(uint8_t* base, uint32_t address, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(rex::memory::GuestPtr(base, address), &value, sizeof(value));
}
float Float(uint8_t* base, uint32_t address) {
  return std::bit_cast<float>(Read(base, address));
}
void Float(uint8_t* base, uint32_t address, double value) {
  Write(base, address, std::bit_cast<uint32_t>(float(value)));
}
OutputState Output() {
  std::lock_guard lock(output_mutex);
  return output_state;
}
Extent Shape(const OutputState& state) {
  return state.display.valid() ? state.display : state.output;
}
bool Trace() {
  return REXCVAR_GET(gta4_trace_aspect) &&
         rex::diagnostics::IsEnabled(rex::diagnostics::Category::kLogging) &&
         trace_count.fetch_add(1, std::memory_order_relaxed) < 96;
}
// A separate bounded budget keeps loading-mask evidence available after splash
// artwork has consumed the general aspect trace budget.
bool TraceLoadingLabel(const UiContext& layout) {
  return layout.active && layout.role == UiRole::kLoadingLabel && REXCVAR_GET(gta4_trace_aspect) &&
         rex::diagnostics::IsEnabled(rex::diagnostics::Category::kLogging) &&
         loading_label_trace_count.fetch_add(1, std::memory_order_relaxed) < 32;
}
void TraceLoadingMask(const UiContext& layout, const Rect& source, const char* kind) {
  if (!TraceLoadingLabel(layout))
    return;
  const Rect mapped = layout.transform.Pixels(layout.render).Map(source);
  REXLOG_INFO(
      "gta4-aspect-loading: {} pixels source={},{},{},{} mapped={},{},{},{} "
      "scale={},{} anchor-offset={},{}",
      kind, source.left, source.top, source.right, source.bottom, mapped.left, mapped.top,
      mapped.right, mapped.bottom, layout.transform.sx, layout.transform.sy, layout.transform.ox,
      layout.transform.oy);
}
constexpr uint32_t kCurrentViewport = 0x831C2200;
constexpr uint32_t kStartupViewport = 0x831C21F4;
constexpr uint32_t kFontStateIndex = 0x82A935A4;
constexpr uint32_t kFontStates = 0x82B9A118;
// The exact address of the HUD object table is generated below from its PPC lis/addi.
constexpr uint32_t kHudTable = 0x82B39990;

uint32_t Owner(uint8_t* base, uint32_t viewport) {
  if (viewport < 16 || !Span(base, viewport - 16, 16))
    return 0;
  return Read(base, viewport - 16);
}
bool DisplayViewport(uint8_t* base, uint32_t viewport, const OutputState& state) {
  if (!state.ready || !Span(base, viewport, 1000))
    return false;
  const Extent size{Read(base, viewport + 688), Read(base, viewport + 692)};
  return (size.width == state.render.width && size.height == state.render.height) ||
         (size.width == state.output.width && size.height == state.output.height);
}
std::optional<double> CameraAspect(uint8_t* base, uint32_t viewport) {
  const auto state = Output();
  if (!DisplayViewport(base, viewport, state))
    return std::nullopt;
  const double width = Float(base, viewport + 672);
  const double height = Float(base, viewport + 676);
  if (!std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
    return std::nullopt;
  const double aspect = Shape(state).aspect() * width / height;
  if (!ScreenCameraOwner(Owner(base, viewport))) {
    // A plain grcViewport copy has no CViewport owner at -16. Recognize only
    // coherent, ALREADY expanded copies. This preserves their projection on
    // subsequent rebuilds without expanding generic/light-space cameras.
    const double authored = Float(base, viewport + 696);
    const double resolved = ExpandVerticalFov(authored, Shape(state).aspect());
    if (!std::isfinite(authored) || !std::isfinite(resolved) ||
        std::abs(resolved - authored) <= 1e-5)
      return std::nullopt;
    const double tangent = std::tan(resolved * (3.14159265358979323846 / 360.0));
    const auto matches = [](double a, double b) {
      return std::isfinite(a) && std::isfinite(b) &&
             std::abs(a - b) <= std::max(1.0, std::abs(b)) * 1e-5;
    };
    if (!matches(Float(base, viewport + 700), aspect) ||
        !matches(Float(base, viewport + 712), tangent) ||
        !matches(Float(base, viewport + 716), tangent * aspect) ||
        !matches(Float(base, viewport + 448) * tangent * aspect, Float(base, viewport + 720)) ||
        !matches(Float(base, viewport + 468) * tangent, Float(base, viewport + 724)))
      return std::nullopt;
  }
  return aspect;
}
void TraceCameraConsumer(uint8_t* base, uint32_t viewport, const char* role) {
  const unsigned category = role[0] == 'a' ? 0 : role[0] == 'd' ? 1 : role[0] == 'p' ? 2 : 3;
  if (!REXCVAR_GET(gta4_trace_aspect) ||
      !rex::diagnostics::IsEnabled(rex::diagnostics::Category::kLogging) ||
      !Span(base, viewport, 1000))
    return;
  // Repeated loading frames must not consume the entire budget before gameplay.
  uint64_t shape = 14695981039346656037ull;
  for (uint32_t offset : {696u, 700u, 712u, 716u, 448u, 468u})
    shape = (shape ^ Read(base, viewport + offset)) * 1099511628211ull;
  if (camera_consumer_trace_shape[category].exchange(shape, std::memory_order_relaxed) == shape ||
      camera_consumer_trace_count[category].fetch_add(1, std::memory_order_relaxed) >= 48)
    return;
  REXLOG_INFO("gta4-aspect-consumer: role={} viewport={:08X} owner={:08X} size={}x{} "
      "fov={} aspect-input={} tangent={},{} projection={},{}",
      role, viewport, Owner(base, viewport), Read(base, viewport+688), Read(base, viewport+692),
      Float(base,viewport+696), Float(base,viewport+700), Float(base,viewport+712),
      Float(base,viewport+716), Float(base,viewport+448), Float(base,viewport+468));
}
std::optional<double> ResolvedConsumerFov(uint8_t* base, uint32_t viewport) {
  // These two exact auxiliary callers accept copied grcViewports as well as
  // owner-embedded main cameras. Copies retain the resolved projection/tangents
  // but deliberately retain the authored (animation input) FOV at +696.
  if (!DisplayViewport(base, viewport, Output()))
    return std::nullopt;
  const double tangent = Float(base, viewport + 712);
  const double projection = Float(base, viewport + 468);
  const double scale = Float(base, viewport + 724);
  if (!std::isfinite(tangent) || tangent <= 0 || !std::isfinite(projection) ||
      !std::isfinite(scale) || scale <= 0 ||
      std::abs(tangent * projection - scale) > std::max(1.0, std::abs(scale)) * 1e-5)
    return std::nullopt;
  const double fov = std::atan(tangent) * (360.0 / 3.14159265358979323846);
  return fov > 0 && fov < 179 ? std::optional<double>{fov} : std::nullopt;
}
UiContext MakeContext(UiRole role, Point anchor, bool safe = true) {
  const auto state = Output();
  const auto shape = Shape(state);
  const auto transform = role == UiRole::kRadarLocal ? Transform{}
      : role == UiRole::kMenuBody ? MenuBodyLayout(shape, anchor.y)
      : safe && role != UiRole::kFixed ? SafeLayout(shape, anchor, state.safe)
                                      : Layout(shape, anchor);
  return {transform, state.render, role, state.ready && role != UiRole::kNone, state.generation};
}
class EmitScope {
 public:
  explicit EmitScope(Emission value) : previous_(emission) {
    if (baked_font_depth)
      emission = {};
    else if (!emission.active)
      emission = value;
  }
  ~EmitScope() { emission = previous_; }

 private:
  Emission previous_;
};
class NoFontTransform {
 public:
  NoFontTransform() : previous_(emission) {
    ++baked_font_depth;
    emission = {};
  }
  ~NoFontTransform() {
    --baked_font_depth;
    emission = previous_;
  }

 private:
  Emission previous_;
};
struct DcLayout {
  uint32_t token, vtable;
  uint64_t serial;
  UiContext context;
};
std::mutex dc_mutex;
std::unordered_map<uint32_t, DcLayout> dc_layouts;
std::deque<std::pair<uint32_t, uint64_t>> dc_order;
uint64_t next_dc_serial = 0;
constexpr size_t kMaximumDcLayouts = 32768;

bool LayoutDc(uint32_t vtable) {
  return HasUiDrawExecutor(vtable);
}

UiContext DcContext(PPCContext& ctx, uint8_t* base) {
  const uint32_t dc = ctx.r3.u32;
  if (!Span(base, dc, 8))
    return ui_context;
  const uint32_t vtable = Read(base, dc), token = StableDcToken(Read(base, dc + 4));
  std::lock_guard lock(dc_mutex);
  const auto it = dc_layouts.find(dc);
  if (it == dc_layouts.end() || it->second.token != token || it->second.vtable != vtable)
    return ui_context;
  return it->second.context;
}
struct HudAnchor {
  bool specified = false, world_position = false;
  Point anchor;
};
std::mutex hud_mutex;
std::unordered_map<uint32_t, HudAnchor> hud_anchors;
HudAnchor NameAnchor(std::string_view name) {
  if (name.starts_with("HUD_MP_NAME"))
    return {false, true, {}};
  if (name.starts_with("HUD_RADAR") || name.starts_with("HUD_PHONE_MESSAGE") ||
      name == "HUD_TEXT_MESSAGE_ICON" || name == "HUD_SLEEP_MODE_ICON")
    return {true, false, {0, 1}};
  if (name.starts_with("HUD_HELP"))
    return {true, false, {0, 0}};
  if (name == "HUD_SUBITILES")
    return {true, false, {0.5, 1}};
  if ((name.starts_with("HUD_WEAPON_") && name != "HUD_WEAPON_ICON") ||
      (name.starts_with("HUD_BIG_MESSAGE_") && name != "HUD_BIG_MESSAGE_TITLE"))
    return {true, false, {0.5, 0.5}};
  return {};
}
UiContext HudContext(PPCContext& ctx, uint8_t* base) {
  if (ui_context.active && ui_context.role != UiRole::kComponent)
    return ui_context;
  const uint32_t id = ctx.r3.u32;
  if (id >= 256 || !Span(base, kHudTable + id * 4, 4))
    return ui_context;
  const uint32_t object = Read(base, kHudTable + id * 4);
  if (!Span(base, object, 40))
    return ui_context;
  const Point point{Float(base, object + 24), Float(base, object + 28)};
  if (!std::isfinite(point.x) || !std::isfinite(point.y))
    return ui_context;
  HudAnchor policy;
  {
    std::lock_guard lock(hud_mutex);
    const auto it = hud_anchors.find(id);
    if (it != hud_anchors.end())
      policy = it->second;
  }
  return MakeContext(UiRole::kComponent, policy.world_position ? point
                                         : policy.specified    ? policy.anchor
                                                               : ComponentAnchor(point), !policy.world_position);
}
void DrawHud(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  const Scope scope(HudContext(ctx, base));
  original(ctx, base);
}
uint32_t FontState(PPCContext& ctx, uint8_t* base) {
  if (!Span(base, kFontStateIndex, 4))
    return 0;
  uint32_t channel = Read(base, kFontStateIndex);
  if (channel == UINT32_MAX) {
    if (!Span(base, ctx.r13.u32, 4))
      return 0;
    const uint32_t tls = Read(base, ctx.r13.u32);
    if (tls > UINT32_MAX - 8 || !Span(base, tls + 8, 4))
      return 0;
    channel = (Read(base, tls + 8) >> 2) & 1;
  }
  if (channel > 1)
    return 0;
  const uint32_t address = kFontStates + channel * 68;
  return Span(base, address, 68, true) ? address : 0;
}
}  // namespace

void ConfigureDisplay(Extent display, SafeInsets insets) {
  auto valid = [](double value) { return std::isfinite(value) && value >= 0 && value < 0.5; };
  if (!display.valid()) display = {};
  if (!display.valid() || !valid(insets.left) || !valid(insets.top) ||
      !valid(insets.right) || !valid(insets.bottom) ||
      insets.left + insets.right >= 1 || insets.top + insets.bottom >= 1)
    insets = {};
  std::lock_guard lock(output_mutex);
  output_state.display = display;
  output_state.safe = insets;
  ++output_state.generation;
}
Extent ConfiguredDisplay(Extent fallback) {
  const auto state = Output();
  return state.display.valid() ? state.display : fallback;
}
void Publish(Extent render, Extent output) {
  if (!render.width || !render.height || !output.width || !output.height)
    return;
  bool changed;
  {
    std::lock_guard lock(output_mutex);
    changed = !output_state.ready || output.width != output_state.output.width ||
              output.height != output_state.output.height ||
              render.width != output_state.render.width ||
              render.height != output_state.render.height;
    output_state = {render, output, true, output_state.generation + uint64_t(changed),
                    output_state.display, output_state.safe};
  }
  if (changed) {
    const auto layout = Layout(output);
    REXLOG_INFO(
        "gta4-aspect: output={}x{} render={}x{} camera-aspect={} ui-scale={},{} "
        "artwork-offset={},{}",
        output.width, output.height, render.width, render.height,
        double(output.width) / output.height, layout.sx, layout.sy, layout.ox, layout.oy);
  }
}
UiContext CurrentUi(uint8_t* base) {
  if (baked_font_depth)
    return {};
  if (ui_context.active)
    return ui_context;
  const auto state = Output();
  if (!state.ready || !Span(base, kCurrentViewport, 4))
    return {};
  const uint32_t viewport = Read(base, kCurrentViewport);
  if (!DisplayViewport(base, viewport, state))
    return {};
  const bool startup = Span(base, kStartupViewport, 4) && viewport == Read(base, kStartupViewport);
  if (!PrimaryUiOwner(Owner(base, viewport)) && !startup)
    return {};
  return {Layout(Shape(state)), state.render, UiRole::kComponent, true, state.generation};
}
UiContext MenuBodyUi(uint8_t* base) {
  // Nested list, slider and hitbox passes must not compute different snapshots.
  if (ui_context.active && ui_context.role == UiRole::kMenuBody)
    return ui_context;
  double divider_y = kDefaultMenuDividerY;
  // Retail frontend style pointer; entry zero is TOP_position_of_top_line.
  constexpr uint32_t kFrontendStylePointer = 0x82BF9D98;
  if (Span(base, kFrontendStylePointer, sizeof(uint32_t))) {
    const uint32_t style = Read(base, kFrontendStylePointer);
    if (Span(base, style, sizeof(float)))
      divider_y = Float(base, style);
  }
  return MakeContext(UiRole::kMenuBody, {0.5, divider_y});
}
UiContext TextUi(const PPCContext& ctx, uint8_t* base) {
  if (ui_context.active)
    return ui_context;
  auto context = CurrentUi(base);
  if (!context.active || !std::isfinite(ctx.f1.f64) || !std::isfinite(ctx.f2.f64))
    return context;
  const Point anchor = ComponentAnchor({ctx.f1.f64, ctx.f2.f64});
  const auto state = Output();
  context.transform = SafeLayout(Shape(state), anchor, state.safe);
  return context;
}
Scope::Scope(UiRole role, Point anchor) : previous_(ui_context) {
  switch (role) {
    case UiRole::kFixed:
      anchor = {0.5, 0.5};
      break;
    case UiRole::kMenuBody:
      anchor = {0.5, kDefaultMenuDividerY};
      break;
    case UiRole::kMenuFooter:
      anchor = {0.5, 1};
      break;
    case UiRole::kRadar:
      anchor = {0, 1};
      break;
    case UiRole::kHelp:
      anchor = {0, 0};
      break;
    case UiRole::kLoadingLabel:
      anchor = {1, 1};
      break;
    default:
      break;
  }
  ui_context = MakeContext(role, anchor);
}
Scope::Scope(UiContext context) : previous_(ui_context) {
  ui_context = context;
}
Scope::~Scope() {
  ui_context = previous_;
}
DcScope::DcScope(PPCContext& ctx, uint8_t* base) : scope_(DcContext(ctx, base)) {}

void FinalizeDc(uint8_t* base, uint32_t dc) {
  if (!Output().ready || !Span(base, dc, 8))
    return;
  const uint32_t vtable = Read(base, dc), token = StableDcToken(Read(base, dc + 4));
  // Some frontend/HUD constructors run outside a component Scope. Preserve
  // their verified display-UI layout at publication; playback can run on a
  // different thread after the producer's viewport and Scope are gone.
  const auto layout = LayoutDc(vtable) ? CurrentUi(base) : UiContext{};
  std::lock_guard lock(dc_mutex);
  // Invalidate on every publication, including reuse by a non-UI command.
  dc_layouts.erase(dc);
  if (!layout.active)
    return;
  const uint64_t serial = ++next_dc_serial;
  dc_layouts.emplace(dc, DcLayout{token, vtable, serial, layout});
  dc_order.emplace_back(dc, serial);
  // Both containers are bounded. Token + vtable prevents address-reuse contamination.
  for (; dc_order.size() > kMaximumDcLayouts; dc_order.pop_front()) {
    const auto [old_dc, old_serial] = dc_order.front();
    const auto it = dc_layouts.find(old_dc);
    if (it != dc_layouts.end() && it->second.serial == old_serial)
      dc_layouts.erase(it);
  }
}
std::optional<Rect> RadarScreenBounds() {
  const auto state = Output();
  std::lock_guard lock(radar_mutex);
  if (!state.ready || radar_bounds_generation != state.generation)
    return std::nullopt;
  return radar_screen_bounds;
}
void PrepareRadarViewport(PPCContext& ctx, uint8_t* base, uint32_t viewport) {
  // sub_8239C9B8 builds the radar render pass's independent orthographic view
  // at pass + 176. Its vertices, stencil mask, rings and blips use local 0..1
  // coordinates. Correct the viewport once, rather than selected inner draws.
  const auto state = Output();
  if (uint64_t(viewport) + 768 > UINT32_MAX ||
      !DisplayViewport(base, viewport, state) || !Span(base, viewport + 640, 128, true))
    return;
  const Extent pixels{Read(base, viewport + 688), Read(base, viewport + 692)};
  const Rect authored{Float(base, viewport + 664), Float(base, viewport + 668),
                      Float(base, viewport + 664) + Float(base, viewport + 672),
                      Float(base, viewport + 668) + Float(base, viewport + 676)};
  // The same pass also services the full pause map. Leave full-output views,
  // offscreen targets, invalid rectangles and unrelated viewports unchanged.
  const double width = authored.right - authored.left, height = authored.bottom - authored.top;
  if (!std::isfinite(authored.left) || !std::isfinite(authored.top) ||
      !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0 ||
      width >= 0.5 || height >= 0.5)
    return;
  const auto transform = SafeLayout(Shape(state), {0, 1}, state.safe);
  if (transform.identity()) {
    std::lock_guard lock(radar_mutex);
    radar_screen_bounds = authored;
    radar_bounds_generation = state.generation;
    return;
  }
  const auto mapped = transform.Map(authored);
  const std::array<double,4> boundaries{mapped.left * pixels.width,
      mapped.top * pixels.height, mapped.right * pixels.width, mapped.bottom * pixels.height};
  // Bound conversion and subsequent signed width/height subtraction even for
  // corrupt guest data. Real viewport boundaries fit in this pixel range.
  if (std::any_of(boundaries.begin(), boundaries.end(), [](double value) {
        return !std::isfinite(value) || value < -32768 || value > 32767;
      })) return;
  // Round the two boundaries together; the game setter rebuilds clipping,
  // pixel-to-clip constants and derived matrices from this canonical window.
  const int32_t left = int32_t(std::lround(mapped.left * pixels.width));
  const int32_t top = int32_t(std::lround(mapped.top * pixels.height));
  const int32_t right = int32_t(std::lround(mapped.right * pixels.width));
  const int32_t bottom = int32_t(std::lround(mapped.bottom * pixels.height));
  PPCContext window = ctx;
  window.r3.u32 = viewport;
  window.r4.s32 = left; window.r5.s32 = top;
  window.r6.s32 = right - left; window.r7.s32 = bottom - top;
  window.f1.f64 = Float(base, viewport + 680);
  window.f2.f64 = Float(base, viewport + 684);
  __imp__sub_828BE238(window, base);
  const double x = Float(base, viewport + 664), y = Float(base, viewport + 668);
  std::lock_guard lock(radar_mutex);
  radar_screen_bounds = {x, y, x + Float(base, viewport + 672), y + Float(base, viewport + 676)};
  radar_bounds_generation = state.generation;
}
void PrepareViewport(PPCContext& ctx, uint8_t* base) {
  const uint32_t viewport = ctx.r3.u32;
  const auto state = Output();
  if (!DisplayViewport(base, viewport, state) || !Span(base, viewport + 448, 280, true))
    return;
  const uint32_t owner = Owner(base, viewport);
  const auto screen_aspect = CameraAspect(base, viewport);
  const bool screen = screen_aspect.has_value(), phone = PhoneCameraOwner(owner);
  if (!screen && !phone)
    return;
  PPCContext query = ctx;
  __imp__sub_821ED2F0(query, base);
  const double aspect = screen_aspect ? *screen_aspect : query.f1.f64;
  const double authored = Float(base, viewport + 696);
  const double fov = screen ? ExpandVerticalFov(authored, Shape(state).aspect()) : authored;
  if (!std::isfinite(fov) || !std::isfinite(aspect) || fov <= 0 || fov >= 179 || aspect <= 0)
    return;
  constexpr double half_radians = 3.14159265358979323846 / 360.0;
  const double tangent = std::tan(fov * half_radians);
  const auto layout = phone ? SafeLayout(Shape(state), {1, 1}, state.safe) : Transform{};
  const double expected_x = Float(base, viewport + 720) / (tangent * aspect) * layout.sx;
  const double expected_y = Float(base, viewport + 724) / tangent * layout.sy;
  const double actual_x = Float(base, viewport + 448), actual_y = Float(base, viewport + 468);
  if (!std::isfinite(expected_x) || !std::isfinite(expected_y))
    return;
  const double w = Float(base, viewport + 492);
  const double offset_x = Float(base, viewport + 728) * layout.sx +
      w * (2 * layout.ox + layout.sx - 1);
  const double offset_y = Float(base, viewport + 732) * layout.sy +
      w * (1 - layout.sy - 2 * layout.oy);
  const bool phone_offset = phone &&
      (std::abs(Float(base, viewport + 480) - offset_x) > 1e-5 ||
       std::abs(Float(base, viewport + 484) - offset_y) > 1e-5);
  if (phone_offset ||
      std::abs(actual_x - expected_x) > std::max(1.0, std::abs(expected_x)) * 1e-5 ||
      std::abs(actual_y - expected_y) > std::max(1.0, std::abs(expected_y)) * 1e-5) {
    // Derived owners can be assigned after the base constructor. At first bind,
    // rebuild both the framing and the shape, not just their aspect quotient.
    PPCContext call = ctx;
    sub_828BDAD8(call, base);
  }
}

void PrepareCameraCopy(PPCContext& ctx, uint8_t* base) {
  // Resolve late-assigned screen owners BEFORE the original complete copy.
  // Otherwise lighting snapshots can capture an unexpanded projection before
  // the main viewport's later bind repairs it for geometry.
  PPCContext source = ctx;
  source.r3 = ctx.r4;
  PrepareViewport(source, base);
}

void PrepareDerivedProjection(PPCContext& ctx, uint8_t* base) {
  // TU8 sub_827BCD28 / sub_827BCF90 construct auxiliary render viewports
  // from the primary camera: r31 / r30 still hold that source grcViewport.
  // Their destination belongs to an offscreen owner, so its projection builder
  // deliberately does not apply the main-camera expansion a second time.
  const uint32_t source = ctx.lr == 0x827BCE90 ? ctx.r31.u32
                        : ctx.lr == 0x827BD198 ? ctx.r30.u32 : 0;
  if (source) TraceCameraConsumer(base, source, "auxiliary-projection");
  const auto resolved = source ? ResolvedConsumerFov(base, source) : std::nullopt;
  if (!resolved)
    return;
  if (std::abs(ctx.f1.f64 - *resolved) > 1e-5)
    ctx.f1.f64 = *resolved;
  if (Trace())
    REXLOG_INFO("gta4-aspect: derived-projection caller={:08X} source={:08X} fov={}",
                uint32_t(ctx.lr), source, ctx.f1.f64);
}

void PrepareDerivedHalfAngle(PPCContext& ctx, uint8_t* base, uint32_t caller) {
  // The second auxiliary path also computes its own tan/sin/cos after the
  // viewport build. Keep all three consistent without modifying a shared
  // camera field, changing camera animation, or expanding light-space views.
  if (ctx.lr != caller)
    return;
  const auto resolved = ResolvedConsumerFov(base, ctx.r30.u32);
  if (!resolved)
    return;
  const double fov = Float(base, ctx.r30.u32 + 696);
  if (std::abs(*resolved - fov) <= 1e-5)
    return;
  // Match the single-precision fmuls used by the original PPC builder.
  ctx.f1.f64 = double(float(float(*resolved) * Float(base, 0x82018970)));
}

void DrawQuad(PPCContext& ctx, uint8_t* base, GuestFunction original, bool textured) {
  auto layout = CurrentUi(base);
  if (!layout.active || emission.active || baked_font_depth) {
    original(ctx, base);
    return;
  }
  Rect bounds{std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
              -std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
  const std::array<uint32_t, 4> vertices = {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32};
  for (uint32_t vertex : vertices) {
    if (!Span(base, vertex, 8)) {
      original(ctx, base);
      return;
    }
    const double x = Float(base, vertex), y = Float(base, vertex + 4);
    if (!std::isfinite(x) || !std::isfinite(y)) {
      original(ctx, base);
      return;
    }
    bounds.left = std::min(bounds.left, x);
    bounds.right = std::max(bounds.right, x);
    bounds.top = std::min(bounds.top, y);
    bounds.bottom = std::max(bounds.bottom, y);
  }
  const bool solid = !textured && Span(base, 0x831C2910, 4) && Span(base, 0x831C2D48, 4) &&
                     Read(base, 0x831C2910) == Read(base, 0x831C2D48);
  if (solid)
    layout.transform = CoveringBackground(layout.transform, bounds);
  const EmitScope emit({layout.transform, !layout.transform.identity()});
  original(ctx, base);
}
void DrawUiVertices(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  const auto layout = CurrentUi(base);
  const EmitScope emit({layout.transform, layout.active && !layout.transform.identity()});
  original(ctx, base);
}
void DrawRadarSection(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  const DcScope dc(ctx, base);
  DrawUiVertices(ctx, base, original);
}
namespace {
void DirtyMenuScissor(uint8_t* base, uint32_t device) {
  // Native submission explicitly invalidates fixed/dynamic state whenever
  // transport word 2 is dirty. Reuse its existing XDK default mask; leave
  // constant, texture-fetch and integer-constant transport words untouched.
  // Restoration must be visible to the following draw too.
  constexpr uint64_t mask = 0x000013A0001809E8;
  const uint32_t address = device + 16;
  const uint64_t value = (uint64_t(Read(base, address)) << 32) | Read(base, address + 4);
  Write(base, address, uint32_t((value | mask) >> 32));
  Write(base, address + 4, uint32_t(value | mask));
}
}
NativeMenuClipScope::NativeMenuClipScope(uint8_t* base, uint32_t device) {
  // Do not use CurrentUi here: baked fonts deliberately disable position
  // correction, but their captured pause context still requires clipping.
  if (!ui_context.active || (ui_context.role != UiRole::kFixed &&
      ui_context.role != UiRole::kMenuBody) || ui_context.transform.identity() ||
      !Span(base, device, 12656, true))
    return;
  const auto state = Output();
  // Pause-map offscreen targets can be drawn inside the compositor as well.
  // Their own viewport/scissor must remain untouched.
  if (!state.ready || Float(base, device + 12640) != 0 ||
      Float(base, device + 12644) != 0 ||
      Float(base, device + 12648) != state.render.width ||
      Float(base, device + 12652) != state.render.height)
    return;
  const auto transform = Layout(Shape(state)).Pixels(state.render);
  const auto panel = transform.Map(Rect{0, 0, double(state.render.width),
                                       double(state.render.height)});
  int left = int(std::ceil(panel.left)), top = int(std::ceil(panel.top));
  int right = int(std::floor(panel.right)), bottom = int(std::floor(panel.bottom));
  const uint32_t packed = Read(base, device + 10436);
  const uint32_t end = Read(base, device + 10440);
  const uint32_t window = Read(base, device + 10432);
  const auto signed15 = [](uint32_t v) { v &= 0x7fff; return int(v & 0x4000 ? v | 0xffff8000u : v); };
  const int dx = packed & 0x80000000u ? 0 : signed15(window);
  const int dy = packed & 0x80000000u ? 0 : signed15(window >> 16);
  // Packed bounds remain meaningful with the API enable bit off: the XDK
  // setter has already intersected them with the active viewport.
  left = std::max(left, int(packed & 0x3fff) + dx);
  top = std::max(top, int((packed >> 16) & 0x3fff) + dy);
  right = std::max(left, std::min(right, int(end & 0x3fff) + dx));
  bottom = std::max(top, std::min(bottom, int((end >> 16) & 0x3fff) + dy));
  if (left < 0 || top < 0 || right > 0x3fff || bottom > 0x3fff)
    return;
  base_ = base; device_ = device;
  saved_ = {packed, end, Read(base, device + 11848)};
  // Absolute bounds explicitly disable window-offset addition.
  Write(base, device + 10436, 0x80000000u | uint32_t(left) | (uint32_t(top) << 16));
  Write(base, device + 10440, uint32_t(right) | (uint32_t(bottom) << 16));
  Write(base, device + 11848, 1);
  DirtyMenuScissor(base, device);
}
NativeMenuClipScope::~NativeMenuClipScope() {
  if (!base_) return;
  Write(base_, device_ + 10436, saved_[0]);
  Write(base_, device_ + 10440, saved_[1]);
  Write(base_, device_ + 11848, saved_[2]);
  DirtyMenuScissor(base_, device_);
}
void DrawWindow(PPCContext& ctx, uint8_t* base, GuestFunction original) {
  // Generated sub_821F6E38 forwards normalized measured bounds unchanged to
  // sub_8224D1E0. Its tessellator preserves those units through sub_828C2290.
  const auto layout = CurrentUi(base);
  const EmitScope emit({layout.transform, layout.active && !layout.transform.identity()});
  original(ctx, base);
}
FrontendLayoutScope::FrontendLayoutScope(PPCContext& ctx, uint8_t* base) {
  const auto layout = CurrentUi(base);
  if (!layout.active || layout.transform.sx >= 1 || ctx.r3.u32 > 2)
    return;
  constexpr uint32_t table = 0x82CC7BD0;
  if (!Span(base, table + ctx.r3.u32 * 4, 4))
    return;
  const uint32_t widget = Read(base, table + ctx.r3.u32 * 4);
  if (!Span(base, widget, 3216, true))
    return;
  // Retail has two column advances at +3112/+3116, followed by origin +3120.
  const double left = Float(base, widget + 3120);
  const double first = Float(base, widget + 3112), second = Float(base, widget + 3116);
  if (!std::isfinite(left) || !std::isfinite(first) || !std::isfinite(second) || first <= 0 ||
      second <= 0 || first > 1 || second > 1 || left < 0 || left > 1)
    return;
  const double extra = 1.0 / layout.transform.sx - 1.0;
  base_ = base;
  addresses_ = {widget + 3120, widget + 3116};
  originals_ = {Read(base, addresses_[0]), Read(base, addresses_[1])};
  replacements_ = {std::bit_cast<uint32_t>(float(left - extra * 0.5)),
                   std::bit_cast<uint32_t>(float(second + extra))};
  for (size_t i = 0; i < addresses_.size(); ++i)
    Write(base, addresses_[i], replacements_[i]);
  if (Trace())
    REXLOG_INFO("gta4-aspect: menu-reflow widget={:08X} extra-width={}", widget, extra);
}
FrontendLayoutScope::~FrontendLayoutScope() {
  if (!base_)
    return;
  for (size_t i = 0; i < addresses_.size(); ++i)
    if (Read(base_, addresses_[i]) == replacements_[i])
      Write(base_, addresses_[i], originals_[i]);
}
}  // namespace gta4::aspect

// Only new strong hooks live here; existing hooks call the shared adapters above.
extern "C" void sub_821ED2F0(PPCContext& ctx, uint8_t* base) {
  const uint32_t viewport = ctx.r3.u32;
  __imp__sub_821ED2F0(ctx, base);
  if (const auto aspect = gta4::aspect::CameraAspect(base, viewport))
    ctx.f1.f64 = *aspect;
}
extern "C" void sub_821499C8(PPCContext& ctx, uint8_t* base) {
  gta4::aspect::PrepareCameraCopy(ctx, base);
  __imp__sub_821499C8(ctx, base);
}
extern "C" void sub_822C7700(PPCContext& ctx, uint8_t* base) {
  gta4::aspect::PrepareCameraCopy(ctx, base);
  __imp__sub_822C7700(ctx, base);
}
extern "C" void sub_828BDAD8(PPCContext& ctx, uint8_t* base) {
  using namespace gta4::aspect;
  const uint32_t viewport = ctx.r3.u32;
  const auto aspect = CameraAspect(base, viewport);
  const auto state = Output();
  const uint32_t previous_phone = phone_projection_build;
  const bool phone =
      DisplayViewport(base, viewport, state) && PhoneCameraOwner(Owner(base, viewport));
  phone_projection_build = phone ? viewport : 0;
  uint32_t authored = 0;
  bool changed = false;
  if (aspect && Span(base, viewport + 696, 4, true)) {
    authored = Read(base, viewport + 696);
    const double original = std::bit_cast<float>(authored);
    const double resolved = ExpandVerticalFov(original, Shape(state).aspect());
    changed = std::isfinite(resolved) && resolved != original;
    if (changed)
      Float(base, viewport + 696, resolved);
    if (Trace())
      REXLOG_INFO(
          "gta4-aspect: camera viewport={:08X} owner={:08X} aspect={} authored-fov={} "
          "resolved-fov={}",
          viewport, Owner(base, viewport), *aspect, original, resolved);
  }
  __imp__sub_828BDAD8(ctx, base);
  // Matrices, cached tangents and frustum remain resolved; the input FOV stays authored.
  if (changed)
    Write(base, viewport + 696, authored);
  phone_projection_build = previous_phone;
  if (aspect) TraceCameraConsumer(base, viewport, "geometry");
}
extern "C" void sub_828BD1D8(PPCContext& ctx, uint8_t* base) {
  using namespace gta4::aspect;
  const uint32_t viewport = ctx.r3.u32;
  if (viewport && phone_projection_build == viewport && Span(base, viewport + 448, 64, true)) {
    std::array<float, 16> matrix;
    for (size_t i = 0; i < matrix.size(); ++i)
      matrix[i] = Float(base, viewport + 448 + uint32_t(i) * 4);
    const auto state = Output();
    TransformProjection(matrix, SafeLayout(Shape(state), {1, 1}, state.safe));
    for (size_t i = 0; i < matrix.size(); ++i)
      Float(base, viewport + 448 + uint32_t(i) * 4, matrix[i]);
  }
  __imp__sub_828BD1D8(ctx, base);
}
extern "C" void sub_82A02158(PPCContext& ctx, uint8_t* base) {
  gta4::aspect::PrepareDerivedHalfAngle(ctx, base, 0x827BD3D0);
  __imp__sub_82A02158(ctx, base);
}
extern "C" void sub_829FFE18(PPCContext& ctx, uint8_t* base) {
  gta4::aspect::PrepareDerivedHalfAngle(ctx, base, 0x827BD3E8);
  __imp__sub_829FFE18(ctx, base);
}
extern "C" void sub_829FFD48(PPCContext& ctx, uint8_t* base) {
  gta4::aspect::PrepareDerivedHalfAngle(ctx, base, 0x827BD3F8);
  __imp__sub_829FFD48(ctx, base);
}
extern "C" void sub_82293878(PPCContext& ctx, uint8_t* base) {
  using namespace gta4::aspect;
  if (REXCVAR_GET(gta4_trace_aspect) && Span(base, kCurrentViewport, 4))
    TraceCameraConsumer(base, Read(base, kCurrentViewport), "deferred-lighting");
  __imp__sub_82293878(ctx, base);
}
extern "C" void sub_822CF9D0(PPCContext& ctx, uint8_t* base) {
  using namespace gta4::aspect;
  if (REXCVAR_GET(gta4_trace_aspect) && Span(base, kCurrentViewport, 4))
    TraceCameraConsumer(base, Read(base, kCurrentViewport), "post-effects");
  __imp__sub_822CF9D0(ctx, base);
}
extern "C" void sub_828C2290(PPCContext& ctx, uint8_t* base) {
  using namespace gta4::aspect;
  if (emission.active && !baked_font_depth) {
    const auto point = emission.transform.Map(Point{ctx.f1.f64, ctx.f2.f64});
    ctx.f1.f64 = point.x;
    ctx.f2.f64 = point.y;
    if (emission.clip_art) {
      ctx.f1.f64 = std::clamp(point.x, emission.art_clip.left, emission.art_clip.right);
      ctx.f2.f64 = std::clamp(point.y, emission.art_clip.top, emission.art_clip.bottom);
      ctx.f7.f64 = std::clamp((ctx.f1.f64 - emission.art_quad.left) /
                                  (emission.art_quad.right - emission.art_quad.left),
                              0.0, 1.0);
      ctx.f8.f64 = std::clamp(
          (ctx.f2.f64 - emission.art_quad.top) / (emission.art_quad.bottom - emission.art_quad.top),
          0.0, 1.0);
    }
  }
  __imp__sub_828C2290(ctx, base);
}
extern "C" void sub_82143C88(PPCContext& ctx, uint8_t* base) {
  using namespace gta4::aspect;
  // Only the actual textured loading layer, not the player's full-output fade quads.
  const auto state = Output();
  if (!state.ready || ctx.lr != 0x821440C0) {
    __imp__sub_82143C88(ctx, base);
    return;
  }
  const Transform transform = Layout(Shape(state)).Pixels(state.render);
  const Rect quad = transform.Map(Rect{ctx.f1.f64, ctx.f2.f64, ctx.f3.f64, ctx.f4.f64});
  if (!(quad.right > quad.left && quad.bottom > quad.top)) {
    __imp__sub_82143C88(ctx, base);
    return;
  }
  const Rect clip =
      transform.Map(Rect{0, 0, double(state.render.width), double(state.render.height)});
  const EmitScope emit({transform, !transform.identity(), true, quad, clip});
  if (Trace())
    REXLOG_INFO("gta4-aspect: fixed-art rect={},{},{},{}", clip.left, clip.top, clip.right,
                clip.bottom);
  __imp__sub_82143C88(ctx, base);
}
extern "C" void sub_8227F458(PPCContext& ctx, uint8_t* base) {
  using namespace gta4::aspect;
  auto layout = CurrentUi(base);
  if (!layout.active || emission.active || baked_font_depth) {
    __imp__sub_8227F458(ctx, base);
    return;
  }
  if (!layout.render.valid()) {
    __imp__sub_8227F458(ctx, base);
    return;
  }
  const Rect bounds{ctx.f1.f64 / layout.render.width, ctx.f2.f64 / layout.render.height,
                    ctx.f3.f64 / layout.render.width, ctx.f4.f64 / layout.render.height};
  if (layout.role == UiRole::kLoadingLabel) {
    // A moving mask can extend past either horizontal screen edge. It is still
    // part of the label composition, not a full-width panel: changing its scale
    // when it crosses an edge would separate it from the adjoining texture.
    // Only an actual whole-output fade bypasses the composition transform.
    const bool full_output =
        std::min(bounds.left, bounds.right) <= 0 && std::max(bounds.left, bounds.right) >= 1 &&
        std::min(bounds.top, bounds.bottom) <= 0 && std::max(bounds.top, bounds.bottom) >= 1;
    if (full_output)
      layout.transform = {};
  } else {
    layout.transform = CoveringBackground(layout.transform, bounds);
  }
  TraceLoadingMask(layout, {ctx.f1.f64, ctx.f2.f64, ctx.f3.f64, ctx.f4.f64}, "solid-mask");
  const EmitScope emit({layout.transform.Pixels(layout.render), !layout.transform.identity()});
  __imp__sub_8227F458(ctx, base);
}
extern "C" void sub_821F6680(PPCContext& ctx, uint8_t* base) {
  using namespace gta4::aspect;
  if (append_depth) {
    // Shadow/outline recursive emissions already have a mapped origin; scale their
    // original offsets once without remapping the origin or font state again.
    ctx.f1.f64 = append_origin.x + (ctx.f1.f64 - append_origin.x) * append_transform.sx;
    ctx.f2.f64 = append_origin.y + (ctx.f2.f64 - append_origin.y) * append_transform.sy;
    __imp__sub_821F6680(ctx, base);
    return;
  }
  const auto layout = CurrentUi(base);
  const uint32_t font = layout.active ? FontState(ctx, base) : 0;
  if (!font || layout.transform.identity()) {
    __imp__sub_821F6680(ctx, base);
    return;
  }
  std::array<uint32_t, kFontScaledOffsets.size()> saved;
  for (size_t i = 0; i < saved.size(); ++i) {
    saved[i] = Read(base, font + kFontScaledOffsets[i]);
    if (!std::isfinite(std::bit_cast<float>(saved[i]))) {
      __imp__sub_821F6680(ctx, base);
      return;
    }
  }
  append_transform = layout.transform;
  append_origin = layout.transform.Map(Point{ctx.f1.f64, ctx.f2.f64});
  if (TraceLoadingLabel(layout)) {
    REXLOG_INFO(
        "gta4-aspect-loading: text normalized source={},{} mapped={},{} "
        "scale={},{} anchor-offset={},{}",
        ctx.f1.f64, ctx.f2.f64, append_origin.x, append_origin.y, layout.transform.sx,
        layout.transform.sy, layout.transform.ox, layout.transform.oy);
  }
  ctx.f1.f64 = append_origin.x;
  ctx.f2.f64 = append_origin.y;
  for (size_t i = 0; i < saved.size(); ++i)
    Float(base, font + kFontScaledOffsets[i],
          std::bit_cast<float>(saved[i]) * FontScale(kFontScaledOffsets[i], layout.transform));
  ++append_depth;
  __imp__sub_821F6680(ctx, base);
  --append_depth;
  for (size_t i = 0; i < saved.size(); ++i)
    Write(base, font + kFontScaledOffsets[i], saved[i]);
}
extern "C" void sub_821F5788(PPCContext& ctx, uint8_t* base) {
  const gta4::aspect::NoFontTransform baked;
  __imp__sub_821F5788(ctx, base);
}
extern "C" void sub_821C4B90(PPCContext& ctx, uint8_t* base) {
  using namespace gta4::aspect;
  std::string name;
  const uint32_t address = ctx.r3.u32;
  if (Output().ready && Span(base, address, 96)) {
    const char* text = reinterpret_cast<const char*>(rex::memory::GuestPtr(base, address));
    const auto* end = static_cast<const char*>(std::memchr(text, 0, 96));
    if (end)
      name.assign(text, end);
  }
  __imp__sub_821C4B90(ctx, base);
  if (!name.empty() && ctx.r3.u32 < 256) {
    std::lock_guard lock(hud_mutex);
    hud_anchors[ctx.r3.u32] = NameAnchor(name);
  }
}
#define ASPECT_HUD_HOOK(address)                            \
  extern "C" void address(PPCContext& ctx, uint8_t* base) { \
    gta4::aspect::DrawHud(ctx, base, __imp__##address);     \
  }
ASPECT_HUD_HOOK(sub_821C4CD8)
ASPECT_HUD_HOOK(sub_821C5148)
ASPECT_HUD_HOOK(sub_821C5480)
ASPECT_HUD_HOOK(sub_821C5660)
ASPECT_HUD_HOOK(sub_821C58E0)
#undef ASPECT_HUD_HOOK
#define ASPECT_DC_HOOK(address)                             \
  extern "C" void address(PPCContext& ctx, uint8_t* base) { \
    const gta4::aspect::DcScope scope(ctx, base);           \
    __imp__##address(ctx, base);                            \
  }
ASPECT_DC_HOOK(sub_821BCF80)
ASPECT_DC_HOOK(sub_821BCEE0)
ASPECT_DC_HOOK(sub_821BD018)
ASPECT_DC_HOOK(sub_821BD528)
#undef ASPECT_DC_HOOK
extern "C" void sub_821BCFA0(PPCContext& ctx, uint8_t* base) {
  // Type 4 emits curved health/armor strips directly through sub_828C2290;
  // its neighboring quads alone were corrected by DrawQuad.
  gta4::aspect::DrawRadarSection(ctx, base, __imp__sub_821BCFA0);
}
extern "C" void sub_821C4148(PPCContext& ctx, uint8_t* base) {
  // DrawHud has an immediate type-4 route as well as the queued executor.
  // Map curved health/armor vertices at their common normalized primitive.
  // EmitScope preserves an already active queued transform, so it applies once.
  gta4::aspect::DrawUiVertices(ctx, base, __imp__sub_821C4148);
}
extern "C" void sub_821BD138(PPCContext& ctx, uint8_t* base) {
  // Unlike the rectangle executors, this command calls sub_828C2290 directly.
  // A DcScope alone retains the layout but never maps its emitted vertices.
  gta4::aspect::DrawRadarSection(ctx, base, __imp__sub_821BD138);
}
extern "C" void sub_821BD218(PPCContext& ctx, uint8_t* base) {
  gta4::aspect::DrawRadarSection(ctx, base, __imp__sub_821BD218);
}
extern "C" void sub_821BD238(PPCContext& ctx, uint8_t* base) {
  gta4::aspect::DrawRadarSection(ctx, base, __imp__sub_821BD238);
}
extern "C" void sub_822551E0(PPCContext& ctx, uint8_t* base) {
  // This is the menu slider pass, not the footer. The retail compositor
  // sub_82255CC8 draws row labels through sub_8229F0F8, then calls this routine
  // to walk type-101 items and emit their border, track and value fill. Both
  // phases must use the same divider-anchored body coordinates, including
  // immediate draw calls and queued text.
  const gta4::aspect::Scope scope(gta4::aspect::MenuBodyUi(base));
  __imp__sub_822551E0(ctx, base);
}

extern "C" void sub_828BF708(PPCContext& ctx, uint8_t* base) {
  __imp__sub_828BF708(ctx, base);
  if (gta4::aspect::Output().ready)
    ctx.r3.u64 = 1;
}
extern "C" void sub_821F7208(PPCContext& ctx, uint8_t* base) {
  const gta4::aspect::Scope scope(gta4::aspect::TextUi(ctx, base));
  __imp__sub_821F7208(ctx, base);
}

// Both pixel-space textured-rectangle adapters meet here: sub_8227F5B8
// supplies default UVs; sub_8227F608 supplies explicit UVs (generated .10).
// Transform their vertex positions once at the common emitter. UVs, colors,
// blend state and animation inputs stay under the original game's control.
extern "C" void sub_8227F2E8(PPCContext& ctx, uint8_t* base) {
  using namespace gta4::aspect;
  const auto layout = CurrentUi(base);
  if (layout.active && layout.render.valid() && !emission.active && !baked_font_depth)
    TraceLoadingMask(layout, {ctx.f1.f64, ctx.f2.f64, ctx.f3.f64, ctx.f4.f64}, "textured-mask");
  const EmitScope emit({layout.transform.Pixels(layout.render),
                        layout.active && layout.render.valid() && !layout.transform.identity()});
  __imp__sub_8227F2E8(ctx, base);
}

extern "C" void sub_821B5C90(PPCContext& ctx, uint8_t* base) {
  // The retail loading label is a composition, not just font glyphs. Generated
  // .4:821B61E0 submits HUD text, then 821B64B0 / 821B652C / 821B6604 draw
  // the translucent solid masks and moving textured strip. Nested HUD text,
  // queued commands and immediate pixel rectangles must use this same anchor.
  const gta4::aspect::Scope layout_scope(gta4::aspect::UiRole::kLoadingLabel);
  __imp__sub_821B5C90(ctx, base);
}
extern "C" void sub_82255CC8(PPCContext& ctx, uint8_t* base) {
  const gta4::aspect::Scope scope(gta4::aspect::MenuBodyUi(base));
  __imp__sub_82255CC8(ctx, base);
}

extern "C" void sub_8214DBD0(PPCContext& ctx, uint8_t* base) {
  // The complete retail pause/frontend compositor includes the heading, tabs,
  // backgrounds and footer outside the existing list/slider hooks. Capture
  // their layout here, before deferred draw playback loses the UI viewport.
  // Nested MenuBodyUi keeps its separate authored divider anchor.
  const gta4::aspect::Scope scope(gta4::aspect::UiRole::kFixed);
  __imp__sub_8214DBD0(ctx, base);
}

extern "C" void sub_8239C468(PPCContext& ctx, uint8_t* base) {
  // This compositor draws inside the radar's orthographic subviewport. The
  // viewport builder owns its screen mapping; retain local vertex coordinates.
  const gta4::aspect::Scope scope(gta4::aspect::UiRole::kRadarLocal);
  __imp__sub_8239C468(ctx, base);
}

extern "C" void sub_8239C9B8(PPCContext& ctx, uint8_t* base) {
  const uint32_t pass = ctx.r3.u32;
  __imp__sub_8239C9B8(ctx, base);
  if (pass && uint64_t(pass) + 176 <= UINT32_MAX)
    gta4::aspect::PrepareRadarViewport(ctx, base, pass + 176);
}

// The frontend appends this command inline, bypassing sub_82146790. Capture the
// component at construction as well; StableDcToken survives size publication.
extern "C" void sub_821BF598(PPCContext& ctx, uint8_t* base) {
  const uint32_t dc = ctx.r3.u32;
  __imp__sub_821BF598(ctx, base);
  gta4::aspect::FinalizeDc(base, dc);
}
