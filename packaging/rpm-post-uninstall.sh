#!/bin/bash
# qZypper RPM %postun script
# Remove SELinux policy module on package removal (not upgrade)

if [ "$1" -eq 0 ] 2>/dev/null; then
    if command -v semodule >/dev/null 2>&1; then
        if semodule -r qzypper; then
            :
        else
            echo "qZypper: warning: 'semodule -r qzypper' failed with exit status $?" >&2
        fi
    fi
fi

# Never fail the RPM transaction because of SELinux setup problems.
exit 0
