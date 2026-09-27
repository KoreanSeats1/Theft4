"""Static checks of real generated PPC call sites and hook linkage contract."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[2]
generated=root/'glue/rexglue-sdk-main/gta4-recomp/generated'
source=(generated/'gta4_recomp.28.cpp').read_text()
hooks=(root/'glue/rexglue-sdk-main/gta4-recomp/src/gta4_physics_hooks.cpp').read_text()
def body(symbol):
    start=source.index('DEFINE_REX_FUNC('+symbol+') {')
    end=source.find('\nDEFINE_REX_FUNC(',start+1)
    return source[start:end if end>=0 else len(source)]
outer=body('sub_824797C0')
stages=['sub_82476B58','sub_82476DA0','sub_82477920']
for stage in stages:
    call=outer.index(stage+'(ctx, base);')
    prefix=outer[max(0,call-250):call]
    assert 'ctx.f1.f64 = double(float(ctx.f0.f64 * ctx.f31.f64));' in prefix, stage
assert outer.index(stages[0]+'(ctx, base);') < outer.index(stages[1]+'(ctx, base);')
assert outer.index(stages[1]+'(ctx, base);') < outer.index('sub_82477EF0(ctx, base);') < outer.index(stages[2]+'(ctx, base);')
assert 'ctx.r4.u64 = ctx.r31.u64;' in outer[outer.index('loc_82479884:'):outer.index(stages[0]+'(ctx, base);')]
assert 'ctx.r3.u64 = ctx.r31.u64;' in outer[outer.index('sub_82477EF0(ctx, base);'):outer.index(stages[2]+'(ctx, base);')]
assert 'ctx.f31.f64 = ctx.f1.f64;' in body(stages[0])[:1000]
assert 'ctx.f31.f64 = ctx.f1.f64;' in body(stages[1])[:1000]
assert 'ctx.f29.f64 = ctx.f1.f64;' in body(stages[2])[:1400]
# Original count reload and loop comparison remain in the authoritative body.
assert outer.count('ctx.r11.u64 = REX_LOAD_U32(ctx.r29.u32 + 17784);')==2
assert 'if (ctx.cr6.lt) goto loc_82479884;' in outer
for stage in ['sub_824797C0',*stages]:
    match=re.search(r'extern "C" void '+stage+r'\(PPCContext& ctx, uint8_t\* base\) \{(.*?)\n\}',hooks,re.S)
    assert match, stage
    wrapper=match.group(1)
    assert wrapper.count('__imp__'+stage+'(ctx, base);')==1,stage
    assert not re.search(r'(?<!__imp__)\b'+stage+r'\(ctx, base\)',wrapper),stage
assert re.findall(r'ctx\.(\w+)(?:\.\w+)?\s*=',hooks)==['f1']
assert 'REX_STORE' not in hooks
assert 'QueryHostTickCount' not in hooks and 'chrono::' not in hooks
cmake=(root/'ios/CMakeLists.txt').read_text()
assert cmake.count('${THEFT4_GTA4_RECOMP_SOURCE_ROOT}/gta4_physics_hooks.cpp')==2
assert re.search(r'gta4_physics_hooks\.cpp"\s+PROPERTIES COMPILE_OPTIONS "-ffp-model=strict"',cmake)
assert 'LINKER:-force_load,$<TARGET_FILE:theft4_gta4_native_compile>' in cmake
header=(generated/'gta4_init.h').read_text()
assert '#define REX_ORIGINAL_FUNC(name) rex_generated_##name' in header
assert 'REX_FUNC(__imp__##name)' in header
print('generated f1 ABI, original stage order/count, four original calls, and force-load linkage contract passed')
