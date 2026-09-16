#include <shader_overrides/shader_override_cache.h>

// The checked-in stock GTA IV SPIR-V cache remains fully available. The
// optional hash-specific modern-shader overrides need a newer host DXC than
// the bundled compiler; keep a valid empty table until that host tool is
// provisioned rather than blocking native renderer bring-up.
const ShaderOverrideCacheEntry g_shaderOverrideEntries[1] = {};
const size_t g_shaderOverrideEntryCount = 0;
