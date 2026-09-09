/* Test-only dyld interpose: deny the explicit process-lifetime retain. */
#include <dlfcn.h>
#include <stddef.h>
static void *deny_pin(const char *path, int flags)
{
 if (flags & RTLD_NODELETE) return NULL;
 return dlopen(path,flags);
}
__attribute__((used,section("__DATA,__interpose")))
static struct {const void *replacement;const void *original;} hook={(const void *)deny_pin,(const void *)dlopen};
