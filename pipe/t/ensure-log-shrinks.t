#!/bin/sh
# shellcheck source=t/preamble.sh
. "$(dirname "$0")"/preamble.sh

n_msgs=50

# write a reasonable amount of data to the log (50 messages × ~32k)
for _ in $(seq $n_msgs); do
    2>/dev/null dd if=/dev/zero bs=32768 count=1 | ./producer "$log"
done

# ensure it's all there
[ "$(du -k "$log" | cut -f1)" -lt 1600 ] && not_ok

# consume until empty
for _ in $(seq $n_msgs); do ./consumer "$log" >/dev/null; done

# ensure it's smaller (should only be a page or two)
[ "$(du -k "$log" | cut -f1)" -gt 16 ] && not_ok

# apparent size should also be smaller
[ "$(du -b "$log" | cut -f1)" -gt 4096 ] && not_ok
ok
