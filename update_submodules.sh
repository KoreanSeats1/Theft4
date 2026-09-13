#!/bin/sh
set -eu
liberty_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$liberty_root/tools/setup_repo.py" "$@"
