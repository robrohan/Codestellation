source "$(dirname "${BASH_SOURCE[0]}")/colors.bash"   # -> lib/colors.bash
log() { echo "${RED}[$(date)]${RESET} $*"; }
