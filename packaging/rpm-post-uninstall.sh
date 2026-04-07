#!/bin/bash
# qZypper RPM %postun script
# Remove SELinux policy module on package removal (not upgrade)

if [ "$1" -eq 0 ] 2>/dev/null; then
    if command -v semodule >/dev/null 2>&1; then
        semodule -r qzypper 2>/dev/null || true
    fi
fi
