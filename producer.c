#include "common.h"

#include <endian.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>


int main(int argc, char **argv)
{
    int log;

    if (2 != argc) errx(EX_USAGE, "usage: producer LOGFILE");
    int open_flags = O_APPEND | O_WRONLY | O_CLOEXEC;
    mode_t open_mode = 0600;
    log = open(argv[1], open_flags, open_mode);
    if (log < 0 && ENOENT == errno) {
        open_flags |= O_CREAT;
        log = open(argv[1], open_flags, open_mode);
    }
    if (log < 0) err(EX_IOERR, "open(%s)", argv[1]);

    if (open_flags & O_CREAT)
        write_exactly(log, (char *)(uint64_t[]){htobe64(sizeof(uint64_t))},
                      sizeof(uint64_t));

    _Static_assert(sizeof(uint64_t) == sizeof(size_t), "assumed size_t was 64-bit");
    size_t avail = PIPE_BUF + sizeof(uint64_t), len = 0;
    char *buffer = malloc(avail);
    if (!buffer) err(EX_UNAVAILABLE, "malloc(%zu)", avail);
    len += sizeof(uint64_t);
    for (;;) {
        ssize_t n = read(0, buffer + len, avail - len);
        if (n < 0 && EINTR == errno) continue;
        if (n < 0) err(EX_IOERR, "read(0)");
        if (n == 0) break;
        len += n;
        if (avail - len < PIPE_BUF) {
            avail <<= 1;
            buffer = realloc(buffer, avail);
            if (!buffer) err(EX_UNAVAILABLE, "realloc(%zu)", avail);
        }
    }

    *(uint64_t *)buffer = htobe64(len - sizeof(uint64_t));
    write_exactly(log, buffer, len);

    //free(buffer);  // if you plan to do something after producing.
    return 0;
}
