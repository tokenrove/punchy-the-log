#include "common.h"
#include <err.h>
#include <errno.h>
#include <sysexits.h>
#include <unistd.h>


void write_exactly(int fd, char *buf, size_t count)
{
    for (;;) {
        ssize_t n = write(fd, buf, count);
        if (n >= 0 && (size_t)n == count) break;
        if (n < 0 && EINTR == errno) continue;  /* interrupted before anything written */
        errx(EX_IOERR, "write(%zu) only wrote %zd; this violates our atomicity constraint",
             count, n);
    }
}
