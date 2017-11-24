#!/bin/sh
# shellcheck source=t/preamble.sh
. "$(dirname "$0")"/preamble.sh

./consumer "$log" 2>/dev/null ||:
echo foo | ./producer "$log"
[ "$(./consumer "$log")" != foo ] && not_ok
ok
