#include <rex/cvar.h>

// The desktop GraphicsSystem normally owns this shared command-processor flag.
// The embedded iOS host links the PM4 processor without that desktop layer.
REXCVAR_DEFINE_STRING(trace_gpu_prefix, "", "GPU", "GPU trace file prefix");
