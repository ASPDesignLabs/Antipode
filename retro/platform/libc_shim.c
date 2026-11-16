/* The one libc function the freestanding WebAssembly build still asks for. (memcpy/memset are wasm bulk-memory
 * instructions; the maths is core.h's own.) */
#include <stddef.h>
size_t strlen(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }
