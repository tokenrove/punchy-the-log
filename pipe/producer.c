#include <stdint.h>
#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>


enum { MAX_VLQ = 10 };

static size_t vlq_encode(size_t v, char *p)
{
    char *o = p;
    if (v >= UINT64_C(9223372036854775808))
        *p++ = 0x80 | ((v>>63) & 0x7f);
    if (v >= UINT64_C(72057594037927936))
        *p++ = 0x80 | ((v>>56) & 0x7f);
    if (v >= UINT64_C(562949953421312))
        *p++ = 0x80 | ((v>>49) & 0x7f);
    if (v >= UINT64_C(4398046511104))
        *p++ = 0x80 | ((v>>42) & 0x7f);
    if (v >= UINT64_C(34359738368))
        *p++ = 0x80 | ((v>>35) & 0x7f);
    if (v >= UINT64_C(268435456))
        *p++ = 0x80 | ((v>>28) & 0x7f);
    if (v >= UINT64_C(2097152))
        *p++ = 0x80 | ((v>>21) & 0x7f);
    if (v >= UINT64_C(16384))
        *p++ = 0x80 | ((v>>14) & 0x7f);
    if (v >= UINT64_C(128))
        *p++ = 0x80 | ((v>>7) & 0x7f);
    *p++ = v&0x7f;
    assert(p-o < MAX_VLQ);
    return p-o;
}


int main(int argc, char **argv)
{
    if (2 != argc)
        errx(EX_USAGE, "usage: producer LOGFILE");
    int fd = open(argv[1], O_APPEND | O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0)
        err(EX_IOERR, "open(%s)", argv[1]);

    size_t avail = PIPE_BUF, count = 0;
    char *message = malloc(avail);
    if (!message)
        err(EX_UNAVAILABLE, "malloc(%zu)", avail);
    for (;;) {
        if (avail-count < PIPE_BUF) {
            avail += avail;
            char *p = realloc(message, avail);
            if (!p)
                err(EX_UNAVAILABLE, "realloc(%zu)", avail);
            message = p;
        }
        ssize_t n_read = read(0, message + count, avail-count);
        if (n_read < 0 && EINTR == errno)
            continue;
        if (n_read < 0)
            err(EX_IOERR, "read");
        if (n_read == 0)
            break;
        count += n_read;
    }

    char len_buf[MAX_VLQ];
    size_t len_count = 0;
    len_count = vlq_encode(count, len_buf);
    if (len_count == 0 && count > 0)
        errx(EX_SOFTWARE, "vlq_encode(%zu) == 0", count);

    struct iovec iov[] = {
        { .iov_base = len_buf, .iov_len = len_count },
        { .iov_base = message, .iov_len = count }
    };
    size_t n_written = 0;
    do {
        ssize_t rv = writev(fd, iov, 2);
        if (rv < 0 && EINTR == errno)
            continue;
        if (rv < 0)
            err(EX_IOERR, "writev");
        n_written += rv;
        if (n_written > len_count) {
            iov[0].iov_len = 0;
            iov[1].iov_base = message + n_written - len_count;
            iov[1].iov_len = count - n_written - len_count;
        }
    } while (n_written < count);

    // free(message);

    if (close(fd))
        warn("close(%s)", argv[1]);
    return 0;
}
