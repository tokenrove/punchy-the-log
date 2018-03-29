#!/bin/sh
# shellcheck source=t/preamble.sh
. "$(dirname "$0")"/preamble.sh

tee "$tmpdir/msg" <<HERE | ./producer "$log"
    WE DON'T NEED TO WRITE IT IN OUR POETRY.  WE WRITE IT IN OUR
    PROGRAMS.  READ OUR OPERATING SYSTEMS, IF YOU WANT TO READ OUR
    POETRY -- THE POETRY OF AN ENGINEER.
HERE
./consumer "$log" | diff "$tmpdir/msg" - || not_ok
ok
