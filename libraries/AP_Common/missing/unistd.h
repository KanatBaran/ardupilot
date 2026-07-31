#if defined(HAVE_UNISTD_H) && HAVE_UNISTD_H
#include_next <unistd.h>
#else

/*
  Hosts without POSIX <unistd.h> (notably native Windows) still need
  ssize_t for APIs that return a byte count or -1 on error.

  This is intentionally not a POSIX syscall shim: call sites that need
  read()/write()/close() must use a real platform API.
 */

#ifndef AP_COMMON_MISSING_UNISTD_H
#define AP_COMMON_MISSING_UNISTD_H

#if defined(_WIN32)
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#else
#include <stddef.h>
typedef ptrdiff_t ssize_t;
#endif

#endif // AP_COMMON_MISSING_UNISTD_H

#endif
