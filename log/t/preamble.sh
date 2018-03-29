set -eu
tmpdir=$(mktemp -d)
trap 'rm -r "$tmpdir"' EXIT
log="$tmpdir"/log
echo 1..1
ok() { echo ok 1; }
not_ok() { echo not ok 1; exit 1; }
