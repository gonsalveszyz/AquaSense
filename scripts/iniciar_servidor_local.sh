#!/bin/sh
set -eu
agua_local_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec python3 "$agua_local_root/scripts/servidor_local.py" start "$@"
