#!/bin/bash
set -euo pipefail
source ./lib/common.sh                  # -> lib/common.sh
. "$(dirname "$0")/env.sh"              # -> env.sh (relative to this script)
./scripts/build.sh --release            # -> scripts/build.sh
bash scripts/test.sh                    # -> scripts/test.sh
source "$CONFIG_FILE"                   # pure expansion: skipped
echo "deployed"
