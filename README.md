# qZypper

**A modern GUI package manager for openSUSE Leap 16 / SLE 16**  

[![License: GPL-2.0-or-later](https://img.shields.io/badge/License-GPL--2.0--or--later-blue.svg)](LICENSE)
[![Qt 6.5+](https://img.shields.io/badge/Qt-6.5%2B-41cd52.svg)](https://www.qt.io/)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C.svg)](https://isocpp.org/)
[![openSUSE](https://img.shields.io/badge/openSUSE-Leap%2016-73ba25.svg)](https://www.opensuse.org/)

> Successor to **YaST Software Management** and **YaST Repository Management**,  
> built with Qt 6 (QML) and libzypp.  

---

<p align="center">
  <img src="src/gui/icons/qZypper@256.png" alt="qZypper" width="256" valign="middle">
  <img src="src/gui/icons/Qt.png" alt="Qt" width="139" valign="middle">
</p>

## Overview

qZypper is a graphical package management tool designed as a replacement for YaST Software Management and  
YaST Repository Management on openSUSE Leap 16 and SUSE Linux Enterprise 16.  

It provides a unified interface for managing packages,  
repositories, services, patterns, and patches through a modern Qt Quick UI,  
with privileged operations handled securely via D-Bus and Polkit authentication.  

## Features

### Package Management

- Search packages by name, keywords, summary, description, provides, requires, recommends, and file list
- Search modes: Auto, Contains, Starts With, Exact Match, Wildcards, Regular Expression, with case-sensitive matching and selectable search targets
- View detailed package information (description, dependencies, changelog, file list)
- Install, update, and remove packages with dependency resolution
- Browse packages by repository or software pattern
- Select specific package versions from multiple repositories
- Set package status: install, update, delete, taboo, protected
- Select multiple rows with Click, Ctrl+Click, and Shift+Click, extend with Shift+Up/Down/Home/End/PageUp/PageDown, or select all with Ctrl+A
- Apply bulk actions to selected rows or to every row in the current list (Install/Update, Delete, Update, Update Unconditionally, Keep/Undo, Taboo, Protected) with a single dependency resolution
- Set status from the keyboard: `+` install, `-` delete, `>` update, `<` keep/undo, `!` taboo, `*` protected
- Install all matching `-devel`, `-debuginfo`, or `-debugsource` packages, or all recommended packages, in one action
- Show or hide `-devel` and `-debuginfo`/`-debugsource` packages in the list

### Package Updates

- Updates tab shows the update count, the oldest last-refresh time among enabled repositories, and buttons for Show Updates, Update All, Distribution Upgrade, and Refresh Repositories
- After Update All, the tab reports how many packages could not be updated automatically
- Optional refresh at startup checks only enabled repositories with auto-refresh whose metadata is stale
- The initialize Polkit action implies refresh-repos, so session start and the startup refresh need a single authentication

### RPM Groups and Languages

- RPM Groups tab shows the RPM group hierarchy with a package count for each group
- Languages tab toggles requested languages and shows the packages for the selected language
- Requested language changes are listed in the commit confirmation

### Solver Options

- Install Recommended Packages is saved between sessions and shows the current `zypp.conf` default
- Clean Up Dependencies When Deleting and Allow Vendor Change apply to the current session only
- Allow Vendor Change does not affect Distribution Upgrade, which sets its own option in the upgrade dialog

### Package Context Menu

- Update is enabled only for an installed package with a newer update candidate and selects the policy candidate
- Update Unconditionally is enabled for an installed package with any candidate version: the same version reinstalls, an older candidate downgrades. It is not offered for Protected packages
- The menu also provides Install, Delete, Taboo, Protected, Undo Changes, persistent Lock/Unlock, and bulk actions for All in This List

### Repository Management

- List, add, remove, and modify repositories
- 2-step wizard for adding repositories (URL, then properties) with URL scheme validation
- Refresh individual or all repositories with progress indication
- Configure priority, auto-refresh, and package caching per repository
- Preview of the URL with repository variables such as `${releasever}` expanded (the raw URL is stored)
- Community repository presets (Packman, VideoLAN, nVidia, and more) for openSUSE Tumbleweed and Leap 16

### Service Management

- Add, remove, and modify RIS package services
- List and refresh plugin services provided by installed packages (they cannot be added, removed, or modified)
- Refresh services (auto-adds associated repositories)

### Pattern & Patch Management

- Browse and install software patterns grouped by category
- View and apply patches by category: security, recommended, optional, feature, document, YaST
- View installed and available products, with the base product marked (read-only)

### Dependency Resolution

- Automatic dependency resolution via libzypp solver
- Interactive conflict resolution with multiple solution options
- Preview pending changes before committing
- Confirm license agreements before applying changes. A rejected license excludes the package (Taboo for a new package, Protected for an update), the solver runs again, and the remaining changes continue
- Choose Abort, Retry, or Ignore when downloading, installing, or removing a package fails
- Install or remove each version of a multiversion package (such as kernels) independently
- Quitting or ending the session with unapplied changes asks for confirmation

### Security

- D-Bus privilege separation (GUI runs unprivileged, backend runs as root)
- systemd-managed D-Bus activation (backend runs as root under a dedicated systemd service)
- Environment sanitization in the systemd unit (fixed `HOME`/`PATH`, inherited variables unset)
- Polkit authentication for privileged operations
- SELinux policy module for Leap 16 / SLE 16
- Dedicated SELinux GPG-agent socket type (`qzypper_gpg_agent_socket_t`) with a socket type transition (policy configuration)

### Internationalization

- English (default) and Japanese language support
- Automatic locale detection and translation loading

## User Interface

### Main Window Tabs

The header toolbar provides nine view tabs.  

| Tab | Description |
|---|---|
| **Search** | Search packages by keyword with configurable search filters |
| **Updates** | Updatable packages, update count, last refresh time, Update All, Distribution Upgrade, and repository refresh |
| **Installation Summary** | Review all pending changes (install / update / delete) before committing |
| **Repository** | Browse packages grouped by repository |
| **Patterns** | Browse and install software patterns grouped by category |
| **Patches** | View and apply patches by category: security, recommended, optional, feature, document, YaST |
| **RPM Groups** | Browse the RPM group hierarchy with package counts |
| **Languages** | Toggle requested languages and view the packages for each language |
| **Products** | Installed and available products, with the base product marked (read-only) |

### Navigation Drawer

The hamburger menu (☰) opens a left-side drawer with the following sections.  

- **Packages**: Update All Packages, Distribution Upgrade, Install All Matching -devel/-debuginfo/-debugsource Packages, Install All Recommended Packages, Apply Changes
- **Options**: Install Recommended Packages, Clean Up Dependencies When Deleting (this session), Allow Vendor Change (this session), Show -devel Packages, Show -debuginfo/-debugsource Packages, Refresh Repositories at Startup
- **Settings**: Manage Repositories, Refresh Repositories, Install Local RPM Files, Package Locks, Installation History (date tree and grouping by command), Verify System
- **Help**: About qZypper, About Qt
- **Quit**

### Repository Manager Drawer

A full-screen right-side drawer with two tabs.  

- **Repositories** — List, add, remove, and edit repositories with a properties panel (priority, enabled, auto-refresh, keep packages)
- **Services** — List, add, remove, and refresh RIS package services; list and refresh plugin services

### Package Details Pane

The bottom pane displays detailed information for the selected package across six tabs.  

| Tab | Content |
|---|---|
| **Description** | Package name, summary, and full description |
| **Technical Data** | Version, architecture, license, group, vendor, build host, build time, install time, size, download size, source RPM, URL |
| **Dependencies** | Requires, provides, conflicts, obsoletes |
| **Version** | Available versions from all repositories with version selection; check boxes per version for multiversion packages |
| **File List** | Files included in the package |
| **Changelog** | Package change history |

### Refresh Progress Overlay

A modal overlay displayed during repository refresh operations,  
featuring a progress indicator, status message, and cancel button.  
Press `Escape` or click the cancel button to cancel the operation; the refresh stops the in-progress repository's libzypp call as soon as it observes the cancellation request.  

## Architecture

![Architecture](docs/architecture.png)  

| Target | Type | Description |
|---|---|---|
| `qzypper` | Executable | GUI application (Qt Quick / QML) |
| `qzypper-backend` | Executable | D-Bus backend service (root, libzypp) |
| `qzypper-zypp` | Static library | ZyppManager (isolated from Qt D-Bus) |
| `qzypper-common` | Interface library | Shared type definitions (PackageInfo, RepoInfo, etc.) |

## Screenshots

<h3 align="center">Package Search</h3>

<p align="center">
  <img src="docs/screenshots/main-window.png" alt="Main Window" width="600">
</p>

<h3 align="center">Browse by Repository</h3>

<p align="center">
  <img src="docs/screenshots/repository-search.png" alt="Browse by Repository" width="600">
</p>

<h3 align="center">Repository Management</h3>

<p align="center">
  <img src="docs/screenshots/repository-management.png" alt="Repository Management" width="600">
</p>

<h3 align="center">Package Details</h3>

<p align="center">
  <img src="docs/screenshots/package-details.png" alt="Package Details" width="600">
</p>

## Requirements

| Dependency | Version | Purpose |
|---|---|---|
| CMake | 3.21+ | Build system |
| GCC | 14.3+ | C++17 compiler |
| Qt 6 | 6.5+ | Core, Quick, QuickControls2, DBus, LinguistTools, Svg |
| libzypp | latest | Package management library |
| PolkitQt6-1 | latest | Polkit authentication (optional, with fallback) |
| Boost.Thread | 1.86+ | Thread management for ZyppManager (imported Boost::thread target linked into qzypper-zypp) |

## Documentation

- [Build and Installation](docs/BUILD.md) — build dependencies, build options, install, installed files, SELinux policy, directory structure
- [D-Bus Interface](docs/DBUS_INTERFACE.md) — backend activation and lifetime, authorization, Polkit actions, methods, signals
- [Configuration File](docs/CONFIGURATION.md) — user settings saved in `~/.config/Presire/qZypper.conf`

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| `[Ctrl] + [Q]` | Quit application |
| `[Ctrl] + [Enter]` | Apply pending changes |
| `[Ctrl] + [A]` | Select all visible packages |
| `[Shift] + [Up]` / `[Shift] + [Down]` | Extend the selection |
| `[Shift] + [Home]` / `[Shift] + [End]` | Extend the selection to the first or last row |
| `[Shift] + [PgUp]` / `[Shift] + [PgDn]` | Extend the selection by one page |
| `+` `-` `>` `<` `!` `*` | Install / delete / update / keep / taboo / protect the selected rows |
| `[Esc]` | Close current drawer / dialog |

## License

This project is licensed under the **GNU General Public License v2.0 or later** - see the [LICENSE](LICENSE) file for details.  

Copyright (C) 2026 Presire  

## Author

**Presire**  

- GitHub: [https://github.com/presire/qZypper](https://github.com/presire/qZypper)
