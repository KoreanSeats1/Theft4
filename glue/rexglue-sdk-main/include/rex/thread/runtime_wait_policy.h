#pragma once

namespace rex::thread {
// Configure once before guest startup. Reconfiguration to a different policy
// requires a process restart so existing waits cannot change semantics.
bool ConfigureRuntimeWaitFixes(bool enabled);
bool RuntimeWaitFixesEnabled();
}  // namespace rex::thread
