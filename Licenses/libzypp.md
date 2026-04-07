# libzypp License Information

**Package:** libzypp
**Website:** https://en.opensuse.org/Libzypp
**Source:** https://github.com/openSUSE/libzypp

**Copyright:**
- (C) 2004-2026 SUSE LLC
- (C) Novell, Inc.
- (C) Various contributors

All rights reserved.

**License:** GNU General Public License v2.0 or later (GPL-2.0-or-later)
**SPDX-License-Identifier:** GPL-2.0-or-later

---

## Description

This application uses libzypp, the package management library that powers zypper and YaST Software Management on openSUSE and SUSE Linux Enterprise. libzypp is licensed under the GNU General Public License version 2 or later.

### libzypp is used in this application for

- Package search, installation, removal, and update
- Repository and service management
- Dependency resolution (SAT solver)
- RPM database access
- Patch and pattern management

### Components used

- libzypp C++ API (`/usr/include/zypp/`)
- ZYpp singleton (`zypp::getZYpp()`)
- RepoManager, PoolQuery, Resolver, Selectable
- ui::Status enumeration for package status tracking

---

## GPL v2 License Summary

The GNU General Public License (GPL) is a free software license that guarantees end users the freedom to run, study, share, and modify the software.

### The GPL v2 ensures that

- You can use the software for any purpose
- You can study and modify the software
- You can redistribute copies
- You can distribute modified versions

> **Note:** This application communicates with libzypp through a D-Bus service boundary (qzypper-backend). The backend process that links directly with libzypp is a separate executable.

**For complete license text, see:**
https://www.gnu.org/licenses/old-licenses/gpl-2.0.html

---

## Source Code Availability

libzypp source code is available at:
https://github.com/openSUSE/libzypp

---

## Additional Resources

- [libzypp API documentation](https://doc.opensuse.org/projects/libzypp/HEAD/)
- [openSUSE libzypp wiki](https://en.opensuse.org/Libzypp)
