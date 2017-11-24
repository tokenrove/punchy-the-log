#define _GNU_SOURCE

#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <err.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sysexits.h>
#include <unistd.h>

#include <sys/inotify.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>


enum { MAX_VLQ = 10 };
/* Note that your file system might have a different block size. */
enum { ASSUMED_BLOCK_SIZE = 4096 };

static int inotify_fd = -1;

static size_t read_or_die(int fd, char *buf, size_t count)
{
block:
    for (int i = 0; i < 50; ++i) {
        ssize_t n = read(fd, buf, count);
        if (n < 0 && EINTR == errno) continue;
        if (n < 0) err(EX_IOERR, "read");
        if (n > 0) return n;
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


static bool vlq_decode(size_t *v, size_t *offset, char *buf, size_t n_avail)
{
    while (*offset < n_avail) {
        uint8_t c = buf[(*offset)++];
        assert((*v<<7) >= *v);
        *v = (*v<<7) | (c & 0x7f);
        if (!(c & 0x80))
            return true;
    }
    return false;
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

    if (argc == 3 && !strcmp(argv[1], "-f")) {
        follow = true;
        ++argv; --argc;
    }
    if (argc != 2)
        errx(EX_USAGE, "usage: consumer [-f] LOGFILE");

    int fd = open(argv[1], O_RDWR | O_CLOEXEC | O_CREAT, 0600);
    if (fd < 0)
        err(EX_IOERR, "open(%s)", argv[1]);

    if (!posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL|POSIX_FADV_NOREUSE))
        err(EX_IOERR, "posix_fadvise(SEQUENTIAL|NOREUSE)");

    if (follow) {
        /* Maybe open inotify for blocking reads. */
        inotify_fd = inotify_init1(IN_CLOEXEC);
        if (-1 != inotify_fd) {
            if (-1 == inotify_add_watch(inotify_fd, argv[1], IN_MODIFY))
                err(EX_OSERR, "inotify_add_watch");
        }
    }

    do {
        off_t start = lseek(fd, 0, SEEK_DATA), end = start;
        if ((off_t)-1 == start && ENXIO == errno)
            start = 0;
        if ((off_t)-1 == start)
            err(EX_IOERR, "lseek(SEEK_DATA)");

        char buf[PIPE_BUF];
        size_t n_read = 0, count = 0, offset = 0;
        do {
            if (n_read-offset == 0) {
                end += offset;
                n_read = read_or_die(fd, buf, sizeof(buf));
                offset = 0;
            }
        } while (!vlq_decode(&count, &offset, buf, n_read) || !count);

        while (count) {
            size_t n = n_read-offset;
            if (n == 0) {
                dump_or_die(fd, count);
                end += count;
                break;
            }
            if (count < n)
                n = count;
            ssize_t rv = write(1, buf+offset, n);
            if (rv < 0 && EINTR == errno)
                continue;
            if (rv < 0)
                err(EX_IOERR, "write");
            count -= rv;
            offset += rv;
        }

        end += offset;
        if (fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, start, end))
            err(EX_IOERR, "fallocate(PUNCH_HOLE)");

        off_t masked = end & ~(UINT64_C(ASSUMED_BLOCK_SIZE-1));
        if (masked > 0 && fallocate(fd, FALLOC_FL_COLLAPSE_RANGE, 0, masked))
            warn("fallocate(COLLAPSE_RANGE)");

        /* You almost certainly don't want to fsync every time, but again,
         * we'll do this for proof of concept. */
        for (;;) {
            int rv = fsync(fd);
            if (rv < 0 && EINTR == errno) continue;
            if (!rv) break;
            err(EX_IOERR, "fsync");
        }
    } while (follow);

    if (close(fd))
        warn("close(%s)", argv[1]);
    return 0;
}
