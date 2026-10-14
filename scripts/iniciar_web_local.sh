#!/bin/bash
set +x
set -eu
task_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
exec python3 "$task_root/scripts/iniciar_web_local.py" "$@"
