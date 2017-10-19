This is a demonstration of the [`fallocate(FALLOC_FL_PUNCH_HOLE,
...)`] technique for keeping an "infinite scroll" journal of
manageable size, as well as a kind of persistent pipe.  The code is
intentionally simple and avoids some performance optimizations.

Each invocation of `producer` writes a length-prefixed message into
the log, and `consumer` reads one out, trimming the log as it goes.
With `-f`, `consumer` will consume continuously.  Exempli gratia:
(requires [pv])

``` shell
$ (IFS= yes | while read x; do echo "$x" | ./producer ./loggy.log; done) &
$ ./consumer -f ./loggy.log | pv >/dev/null
$ kill $!
```

There's an idea of trimming the logical size, from Carlo Alberto
Ferraris's [Persistent "pipes" in Linux], with
`FALLOC_FL_COLLAPSE_RANGE`.  That's not in this version, but I'll
probably add it shortly.

This is extremely Linux-specific.  Portability patches would be
interesting.

[Sparse files] are useful for all kinds of things ([this LWN comment]
gives an example of using this for rewinding live TV), and maybe
aren't as well known as they should be.  Many modern filesystems
support this kind of thing.

Depending on how often data is produced, at EOF you may want the
consumer to spin (repeatedly read) or use `inotify` to get notified of
a change.  The former will tend to give lower latency, but burns a lot
of CPU (maybe yield between reads?); the latter is friendly to other
processes but introduces significant latency.  In this implementation,
we spin a few times and then block.

This consumer trims on every message, but it would be much faster to
only trim every so often, if you have a way to deal with re-reading
duplicates in the case of a crash.

In the multiple-consumer case, you probably want a separate log per
consumer, although you could have some other synchronization
mechanism.  One I've used before is having a separate trim process run
from cron, when the data had timestamps and there were known freshness
constraints.  That doesn't look as cool as this implementation,
though.

In the multiple-producer case, you want to make sure you're writing
your whole message in one `write` call, and if you're really paranoid,
make sure you're [writing less than PIPE_BUF bytes].

Particularly if you're okay with at-least-once consuming, you could
avoid the offset at the beginning by using [`lseek`]`(..., SEEK_DATA,
...)` in the consumer, and starting the file with a hole.


[`fallocate(FALLOC_FL_PUNCH_HOLE, ...)`]: http://man7.org/linux/man-pages/man2/fallocate.2.html
[Persistent "pipes" in Linux]: https://gist.github.com/CAFxX/571a1558db9a7b393579
[pv]: http://www.ivarch.com/programs/pv.shtml
[Sparse files]: https://en.wikipedia.org/wiki/Sparse_file
[this LWN comment]: https://lwn.net/Articles/416234/
[writing less than PIPE_BUF bytes]: http://pubs.opengroup.org/onlinepubs/9699919799/functions/write.html
[`lseek`]: http://man7.org/linux/man-pages/man2/lseek.2.html
