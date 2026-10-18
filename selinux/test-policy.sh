#!/bin/bash
# qZypper SELinux policy smoke test (run on a real Leap 16 / Tumbleweed host).
#
# Usage: sudo selinux/test-policy.sh <build-dir>
#
# 1. Loads the freshly built qzypper.pp module and relabels the binaries.
# 2. Restarts the backend in enforcing mode.
# 3. Asks you to exercise the GUI (refresh, install/remove, lock, history,
#    local RPM, .repo import, commit with snapper) and then prints every AVC
#    denial for the qzypper domains raised since the test started.
set -euo pipefail

BUILD_DIR=${1:?usage: $0 <build-dir>}
PP="$BUILD_DIR/selinux/qzypper.pp"

[[ $EUID -eq 0 ]] || { echo "must be run as root" >&2; exit 1; }
[[ -f $PP ]] || { echo "policy module not found: $PP" >&2; exit 1; }
command -v ausearch >/dev/null || { echo "ausearch (audit) is required" >&2; exit 1; }

echo "SELinux mode: $(cat /sys/fs/selinux/enforce 2>/dev/null | sed 's/1/enforcing/;s/0/permissive/')"

# ausearch parses -ts with the locale's %x date format and expects date and
# time as two separate arguments; pin the C locale so both sides agree.
export LC_ALL=C
START_DATE=$(date '+%x')
START_TIME=$(date '+%H:%M:%S')
semodule -i "$PP"
restorecon -v /usr/bin/qzypper /usr/libexec/qzypper-backend 2>/dev/null || true
pkill -f qzypper-backend || true

echo
echo "Module loaded. Now exercise qZypper as a normal user:"
echo "  refresh repos, search, install/remove, dist-upgrade, patches,"
echo "  add/remove lock, history, verify, solver testcase, local RPM,"
echo "  .repo import, commit (snapper snapshot + restart detection)."
read -r -p "Press Enter when finished... "

echo
echo "== AVC denials since $START_DATE $START_TIME =="
# ausearch exits 1 both for "no matches" and for real errors, so inspect its
# output instead of trusting the exit status.
RAW=$(ausearch -m AVC,USER_AVC,SELINUX_ERR -ts "$START_DATE" "$START_TIME" 2>&1 || true)
if grep -qE '^(Invalid|Error parsing)' <<<"$RAW"; then
    echo "ausearch failed: $RAW" >&2
    exit 1
fi
HITS=$(grep -E 'qzypper(_dbus|_exec|_dbus_exec)?_t' <<<"$RAW" || true)
if [[ -n $HITS ]]; then
    echo "$HITS"
    echo
    echo "Suggested rules (review before adding to qzypper.te):"
    grep -E 'type=AVC' <<<"$HITS" | audit2allow || true
    exit 2
fi
echo "none"
