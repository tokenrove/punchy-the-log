#!/bin/sh
# shellcheck source=t/preamble.sh
. "$(dirname "$0")"/preamble.sh

# feed a bunch of random data through the pipe, make sure it comes out
# the other end the same

n_msgs=50

for _ in $(seq $n_msgs); do
    bs=$(shuf -i 1000-50000 -n 1)
    count=$(shuf -i 1-20 -n 1)
    2>/dev/null dd if=/dev/urandom bs=$bs count=$count > block
    ./producer "$log" < block
    ./consumer "$log" | diff -q - block || not_ok
done
ok