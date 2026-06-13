// Process-wide allocator override for the nz_tenancy binary.
//
// This single TU MUST be compiled into the final executable (not into any
// intermediate static library) for the global `operator new` / `operator delete`
// replacements to take effect. Linking libmimalloc into an intermediate static
// archive does not reliably override the allocator — the linker resolution
// order is not guaranteed to prefer mimalloc's symbols over glibc's.
//
// Verified at startup by main.cpp::verify_mimalloc_override() — if for any
// reason this TU is not actually linked, the probe there aborts loudly with
// a diagnostic rather than letting the binary silently fall back to glibc.

#include <mimalloc-new-delete.h>
