#define _GNU_SOURCE
#include "common.h"

#include <assert.h>
#include <endian.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <sysexits.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/sendfile.h>
#include <sys/types.h>


static int inotify_fd = -1;

static void blocking_read_exactly(int fd, char *buf, size_t count)
{
    for (;;) {
        ssize_t n = read(fd, buf, count);
        if (n < 0 && EINTR == errno) continue;
        if (n >= 0 && (size_t)n == count) break;
        if (n == 0) goto block;
        err(EX_IOERR, "read");
    }
    return;

block:
    for (int i = 0; i < 50; ++i) {
        ssize_t n = read(fd, buf, count);
        if (n < 0 && EINTR == errno) continue;
        if (n >= 0 && (size_t)n == count) return;
        if (n == 0) continue;
        err(EX_IOERR, "read");
    }
    /* Give up and wait on inotify; might actually be better to exit
     * with nonzero status but no error message for the common case of
     * non-blocking checking if one message is present. */
    if (inotify_fd < 0)
        errx(EX_SOFTWARE, "gave up spinning but have no inotify instance");
    _Alignas(struct inotify_event) char in_buf[PIPE_BUF];
    ssize_t n = read(inotify_fd, in_buf, sizeof(in_buf));
    if (n < 0) err(EX_IOERR, "read(inotify_fd)");

    /* Properly, we should now set inotify_fd nonblocking, read until
     * we've emptied the backlog, and then make sure we can read from
     * the log, but that's way too much hassle for a toy like this. */
    goto block;
}


static void trim(int log, size_t offset)
{
    _Static_assert(sizeof(uint64_t) == sizeof(size_t), "assumed size_t was 64-bit");
    uint64_t written_offset = htobe64(offset);
    for (;;) {
        ssize_t n = pwrite(log, &written_offset, sizeof(written_offset), 0);
        if (n < 0 && EINTR == errno) continue;
        if (n < 0) err(EX_IOERR, "pwrite");
        if (sizeof(offset) == n) break;
        errx(EX_SOFTWARE,
             "pwrite(%zu) wrote %zd; log left in inconsistent state",
             offset, n);
    }

    /* It would be better to only punch a hole when we know another
     * block will be actually be excised, and to only punch on block
     * boundaries so we avoid the cost of zeroing.  This exercise is
     * left to the dilligent reader. */
    if (fallocate(log, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
                  sizeof(uint64_t), offset - sizeof(uint64_t)))
        err(EX_IOERR, "fallocate(%zu)", offset);

    /* You almost certainly don't want to fsync every time, but again,
     * we'll do this for proof of concept. */
    for (;;) {
        int rv = fsync(log);
        if (rv < 0 && EINTR == errno) continue;
        if (!rv) break;
        err(EX_IOERR, "fsync");
    }
}


static void dump_or_die(int log, size_t len)
{
    while (len > 0) {
        /* copy_file_range(2) is also interesting. */
        ssize_t n = sendfile(1, log, NULL, len);
        if (n < 0 && EINVAL == errno) goto fallback;
        if (n < 0) err(EX_IOERR, "sendfile(%zu)", len);
        if ((size_t)n > len) errx(EX_SOFTWARE, "sendfile(%zu) = %zd", len, n);
        len -= n;
    }
    return;

fallback:
    for (char buffer[PIPE_BUF]; len > 0;) {
        size_t to_read = len < sizeof(buffer) ? len : sizeof(buffer);
        ssize_t n_read = read(log, buffer, to_read);
        if (n_read < 0 && EINTR == errno) continue;
        if (n_read < 0) err(EX_IOERR, "read");
        len -= n_read;
        ssize_t written = write(1, buffer, n_read);
        /* Don't even bother coping here. */
        assert(written == n_read);
    }
}


int main(int argc, char **argv)
{
    bool follow = false;
    if (3 == argc && !strcmp(argv[1], "-f")) {
        argv[1] = argv[2];
        follow = true;  // -f for foolishly follow forever
    } else if (2 != argc)
        errx(EX_USAGE, "usage: consumer [-f] LOGFILE");

    int open_flags = O_RDWR | O_CLOEXEC;
    int open_mode = 0600;
    int log = open(argv[1], open_flags, open_mode);
    if (log < 0 && ENOENT == errno) {
        open_flags |= O_CREAT;
        log = open(argv[1], open_flags, open_mode);
    }
    if (log < 0)
        err(EX_NOINPUT, "open(%s)", argv[1]);

    if (follow) {
        /* Maybe open inotify for blocking reads. */
        inotify_fd = inotify_init1(IN_CLOEXEC);
        if (-1 != inotify_fd) {
            if (-1 == inotify_add_watch(inotify_fd, argv[1], IN_MODIFY))
                err(EX_OSERR, "inotify_add_watch");
        }
    }

    _Static_assert(sizeof(uint64_t) == sizeof(size_t), "assumed size_t was 64-bit");
    uint64_t origin = sizeof(uint64_t);
    if (open_flags & O_CREAT)
        write_exactly(log, (char *)(uint64_t[]){htobe64(sizeof(uint64_t))},
                      sizeof(uint64_t));
    else {
        blocking_read_exactly(log, (char *)&origin, sizeof(uint64_t));
        origin = be64toh(origin);
    }

    if ((off_t)-1 == lseek(log, origin, 0))
        err(EX_IOERR, "lseek(%zu)", origin);

    uint64_t offset = origin;
    do {
        size_t len;
        blocking_read_exactly(log, (char *)&len, sizeof(uint64_t));
        len = be64toh(len);
        offset += sizeof(uint64_t);

        dump_or_die(log, len);
        /* could lseek(SEEK_CUR) here, if you're suspicious. */
        offset += len;
        trim(log, offset);
    } while (follow);

    return 0;
}
