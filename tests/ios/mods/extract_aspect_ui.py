"""Exercise shipping UI adapters with original PPC geometry/window emitters.

Guest page checks, GPU state/submission, font append, and compositor/pass setup
are doubled. Generic callback argument loaders and actual circle/ring/map/window
emitters are extracted from generated code. The checked function-table dispatcher
routes callbacks to those primitives, without emulating their GPU setup bodies.
This verifies CPU layout contracts, not complete live-game rendering.
"""
import sys
from pathlib import Path
root, destination = map(Path, sys.argv[1:3])
hooks_path = Path(sys.argv[3]) if len(sys.argv) > 3 else (
    root / 'glue/rexglue-sdk-main/gta4-recomp/src/gta4_aspect_hooks.cpp')
hooks = hooks_path.read_text()
def extract(source, signature):
    begin = source.index(signature)
    brace = source.index('{', begin)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]
def item(signature, suffix=''):
    return extract(hooks, signature) + suffix
header = ['namespace gta4::aspect {', item('struct OutputState {',';'),
    'std::mutex output_mutex; OutputState output_state;',
    'std::mutex radar_mutex; Rect radar_screen_bounds{}; uint64_t radar_bounds_generation=UINT64_MAX;',
    'thread_local UiContext ui_context;',
    'thread_local unsigned baked_font_depth=0, append_depth=0;',
    'thread_local Point append_origin; thread_local Transform append_transform;',
    item('struct Emission {',';'), 'thread_local Emission emission;',
    'bool Span(uint8_t* b,uint32_t a,size_t s,bool = false) { '
    'return b && a && s && uint64_t(a)+s <= (UINT64_C(1)<<32); }',
    'bool Trace() {return false;}',
    'bool TraceLoadingLabel(const UiContext&) {return false;}',
    'void TraceLoadingMask(const UiContext&, const Rect&, const char*) {}',
    'constexpr uint32_t kCurrentViewport=0x831C2200, kStartupViewport=0x831C21F4, '
    'kFontStateIndex=0x82A935A4, kFontStates=0x82B9A118;']
header += [item(s) for s in ['uint32_t Read(', 'void Write(', 'float Float(', 'void Float(',
    'OutputState Output()', 'Extent Shape(', 'uint32_t Owner(', 'bool DisplayViewport(',
    'UiContext MakeContext(']]
header += [item('class EmitScope {',';'),item('class NoFontTransform {',';'),
    item('struct DcLayout {',';'),
    'std::mutex dc_mutex; std::unordered_map<uint32_t,DcLayout> dc_layouts; '
    'std::deque<std::pair<uint32_t,uint64_t>> dc_order; uint64_t next_dc_serial=0; '
    'constexpr size_t kMaximumDcLayouts=32768;']
header += [item(s) for s in ['bool LayoutDc(', 'UiContext DcContext(', 'uint32_t FontState(',
    'void ConfigureDisplay(', 'void Publish(', 'UiContext CurrentUi(', 'UiContext MenuBodyUi(',
    'Scope::Scope(UiRole', 'Scope::Scope(UiContext', 'Scope::~Scope()',
    'DcScope::DcScope(', 'void FinalizeDc(', 'void DirtyMenuScissor(', 'NativeMenuClipScope::NativeMenuClipScope(', 'NativeMenuClipScope::~NativeMenuClipScope()']]
if 'void DrawUiVertices(' in hooks:
    header.append(item('void DrawUiVertices('))
header.append(item('void DrawRadarSection('))
if 'std::optional<Rect> RadarScreenBounds(' in hooks:
    header.append(item('std::optional<Rect> RadarScreenBounds('))
header.append(item('void PrepareRadarViewport(') if 'void PrepareRadarViewport(' in hooks else 'void PrepareRadarViewport(PPCContext&,uint8_t*,uint32_t) {}')

header += ['}']
# The wrapper used by the normalized quad executor changed from scope-only to
# scope + emission. Extract the macro route too when checking a pre-fix source.
if 'ASPECT_DC_HOOK(sub_821BD138)' in hooks:
    header.append('extern "C" void sub_821BD138(PPCContext& ctx,uint8_t* base) {'
        'const gta4::aspect::DcScope scope(ctx,base); __imp__sub_821BD138(ctx,base);}')
else:
    header.append(item('extern "C" void sub_821BD138('))
for name in ['sub_828C2290','sub_8227F458','sub_821F6680','sub_821F5788']:
    header.append(item('extern "C" void '+name+'('))
for name in ['sub_8214DBD0','sub_8239C468']:
    signature = 'extern "C" void '+name+'('
    header.append(item(signature) if signature in hooks else (
        signature+'PPCContext& ctx,uint8_t* base) { __imp__'+name+'(ctx,base); }'))
for name in ['sub_821BCFA0','sub_821BCEE0']:
    signature='extern "C" void '+name+'('
    header.append(item(signature) if signature in hooks else (signature+'PPCContext& ctx,uint8_t* base) {'
        'const gta4::aspect::DcScope scope(ctx,base); __imp__'+name+'(ctx,base); }'))
for name in ['sub_821FC590','sub_823337E8','sub_82334C98','sub_823339E0']:
    header.append('extern "C" void '+name+'(PPCContext& ctx,uint8_t* base) { __imp__'+name+'(ctx,base); }')
header.append('extern "C" void sub_821BD0A0(PPCContext& ctx,uint8_t* base) {'
              'gta4::aspect::DrawRadarSection(ctx,base,__imp__sub_821BD0A0);}')
signature='extern "C" void sub_8239C9B8('
header.append(item(signature) if signature in hooks else signature+'PPCContext& ctx,uint8_t* base) { __imp__sub_8239C9B8(ctx,base); }')
signature='extern "C" void sub_821C4148('
header.append(item(signature) if signature in hooks else (
    signature+'PPCContext& ctx,uint8_t* base) { __imp__sub_821C4148(ctx,base); }'))
header.append('extern "C" void sub_821BD528(PPCContext& ctx,uint8_t* base) {'
    'const gta4::aspect::DcScope scope(ctx,base); __imp__sub_821BD528(ctx,base);}')
names = {'sub_821BD138','sub_821BD528','sub_8227F658','sub_8227F458','sub_828C2290',
         '__savegprlr_28','__restgprlr_28','__savefpr_25','__restfpr_25',
         'sub_821BCFA0','sub_821C4148','__savegprlr_26','__restgprlr_26',
         '__savefpr_22','__restfpr_22', 'sub_821FC590','sub_823337E8',
         'sub_82334C98','sub_823339E0','sub_828BE238','sub_821BD0A0','__savegprlr_24','__restgprlr_24',
         '__savefpr_24','__restfpr_24','__savefpr_20','__restfpr_20'}
found = {}
for path in sorted((root / 'glue/rexglue-sdk-main/gta4-recomp/generated').glob('gta4_recomp.*.cpp')):
    source = path.read_text()
    for name in names:
        signature = f'DEFINE_REX_FUNC({name})'
        if signature in source:
            body = extract(source, signature)
            if name in {'sub_821BD138','sub_821BD528','sub_8227F458','sub_828C2290','sub_821BCFA0','sub_821C4148','sub_821FC590','sub_823337E8','sub_82334C98','sub_823339E0','sub_828BE238','sub_821BD0A0'}:
                body = body.replace(signature,
                    f'extern "C" void __imp__{name}(PPCContext& ctx,uint8_t* base)',1)
            found[name]=body
assert found.keys()==names, names-found.keys()
header += [found[name] for name in sorted(found)]
destination.write_text('\n\n'.join(header)+'\n')
