"""Exercise shipping mod hooks against original TU8 counter/flag code.

Only guest memory validation and unrelated weapon ballistics/script scheduling
are doubles. The firing consumption block, reload, native flag setters, health
leaf and local-player lookup are extracted from the generated game functions.
"""
import re
import sys
from pathlib import Path
root, output = map(Path, sys.argv[1:])
src = root / 'glue/rexglue-sdk-main/gta4-recomp'
def body(text, signature):
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]
hooks = (src / 'src/gta4_gameplay_mods.cpp').read_text()
span = body(hooks, 'bool Span(')
hooks = hooks.replace(span, '''bool Span(uint8_t* b, uint32_t a, size_t s, bool = false) {
 return b && a && s && uint64_t(a)+s <= (UINT64_C(1)<<32) &&
 !(a <= denied_address && uint64_t(a)+s > denied_address);
}''')
hooks = re.sub(r'^#include.*$', '', hooks, flags=re.M)
names = {'sub_82238C28','sub_8247E8A0','sub_82267588','sub_825B5E80',
         'sub_825C85E8','sub_825BBC50','sub_825C4690','sub_825C4710','sub_825C7CD0',
         'sub_8226F428','sub_825C3C38','sub_825C90E8'}
functions = {}
for path in sorted((src / 'generated').glob('gta4_recomp.*.cpp')):
    text = path.read_text()
    for name in names - functions.keys():
        signature = f'DEFINE_REX_FUNC({name})'
        if signature in text:
            functions[name] = body(text, signature)
    if len(functions) == len(names): break
assert len(functions) == len(names)
header = ['uint32_t denied_address = 0;', hooks]
for name, fn in functions.items():
    if name == 'sub_8226F428':
        start = fn.index('// lhz r11,28(r27)')
        end = fn.index('loc_8226F790:', start)
        block = fn[start:end]
        header.append('''extern "C" void __imp__sub_8226F428(PPCContext& ctx, uint8_t* base) {
 ctx.r27.u32 = ctx.r3.u32;
''' + block + '''
loc_8226F790:
 ctx.r3.u32 = 0x55;
 ctx.r4.u32 = 0x66;
}''')
    elif name == 'sub_82267588': header.append(fn)
    else:
        header.append(fn.replace(f'DEFINE_REX_FUNC({name})',
            f'extern "C" void __imp__{name}(PPCContext& ctx, uint8_t* base)'))
output.write_text('\n'.join(header))
