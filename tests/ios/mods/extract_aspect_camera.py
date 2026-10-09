"""Build-only extraction of original PPC camera math and the shipping adapters.

No copy of game files or permanent fork of generated functions is maintained.
Only guest page validation, logging, tan libm and the GPU constant publication
are test doubles; projection, view multiplication and frustum code are real.
"""
import re
import sys
from pathlib import Path
root, destination = map(Path, sys.argv[1:])
sdk = root / 'glue/rexglue-sdk-main'
hooks = (sdk / 'gta4-recomp/src/gta4_aspect_hooks.cpp').read_text()
def extract(source, signature):
    begin = source.index(signature)
    brace = source.index('{', begin)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]
helpers = [
    'struct OutputState {', 'uint32_t Read(', 'void Write(',
    'float Float(', 'void Float(', 'OutputState Output()',
    'Extent Shape(', 'uint32_t Owner(', 'bool DisplayViewport(',
    'std::optional<double> CameraAspect(', 'void ConfigureDisplay(',
    'std::optional<double> ResolvedConsumerFov(',
    'Extent ConfiguredDisplay(', 'void Publish(', 'void PrepareViewport(',
    'void PrepareCameraCopy(',
    'void PrepareDerivedProjection(', 'void PrepareDerivedHalfAngle(',
]
header = ['namespace gta4::aspect {', extract(hooks, helpers[0]) + ';',
    'std::mutex output_mutex; OutputState output_state;',
    'thread_local uint32_t phone_projection_build = 0;',
    'bool Span(uint8_t* b, uint32_t a, size_t s, bool = false) { '
    'return b && a && s && uint64_t(a)+s <= (UINT64_C(1)<<32); }',
    'bool Trace() { return false; }',
    'void TraceCameraConsumer(uint8_t*, uint32_t, const char*) {}']
header += [extract(hooks, signature) for signature in helpers[1:]]
header += ['}']
for name in ['sub_821ED2F0','sub_828BDAD8','sub_828BD1D8']:
    header.append(extract(hooks, 'extern "C" void ' + name + '('))
# Include exactly the dependency closure for the camera math. The active-GPU
# publication branch is excluded from this CPU fixture by its guest state.
names = {'sub_821ED2F0','sub_828BDAD8','sub_828BD1D8','sub_828BD770',
         'sub_828BE580','sub_822707F8','__savefpr_22','__restfpr_22'}
found = {}
for path in sorted((sdk / 'gta4-recomp/generated').glob('gta4_recomp.*.cpp')):
    source = path.read_text()
    for name in names:
        signature = f'DEFINE_REX_FUNC({name})'
        if signature in source:
            body = extract(source, signature)
            # Strong wrappers above replace the generated entry but call the
            # untouched original through __imp__, just like the shipping game.
            if name in {'sub_821ED2F0','sub_828BDAD8','sub_828BD1D8','sub_828BE580'}:
                body = body.replace(signature, f'extern "C" void __imp__{name}(PPCContext& ctx, uint8_t* base)',1)
            found[name] = body
assert found.keys() == names, f'Missing generated bodies: {names-found.keys()}'
header += [found[name] for name in sorted(found)]
destination.write_text('\n\n'.join(header)+'\n')
