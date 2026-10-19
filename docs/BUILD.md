# Build and Installation

[日本語](BUILD_JP.md)  

## Build Dependencies (openSUSE)

Install the required development packages.  

```bash
sudo zypper install cmake gcc-c++ pkg-config \
  qt6-base-common-devel qt6-declarative-devel qt6-quick-devel \
  qt6-quickcontrols2-devel qt6-dbus-devel qt6-linguist-devel qt6-svg-devel \
  libzypp-devel libboost_thread-devel \
  libpolkit-qt6-1-devel
```

To build the SELinux policy module (optional):

```bash
sudo zypper install selinux-policy-devel
```

> **Note**:  
> If Qt 6.5+ is not available in the distribution repository (e.g. openSUSE Leap 15.x),  
> install it manually from the [Qt online installer](https://www.qt.io/download-qt-installer-oss) and specify the path via `-DCMAKE_PREFIX_PATH`.  

## Build

```bash
# Configure
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# If Qt is installed in a custom location
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
                    -DCMAKE_PREFIX_PATH=/path/to/Qt/6.5.3/gcc_64

# Build
cmake --build build
```

### Build Options

| Option | Default | Description |
|---|---|---|
| `ENABLE_SELINUX` | `ON` | Build and install SELinux policy module |

```bash
# Disable SELinux policy module
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DENABLE_SELINUX=OFF
```

## Install

```bash
# Install to system
sudo cmake --install build

# Create RPM package
cd build && cpack -G RPM
```

### Installed Files

| File | Location |
|---|---|
| GUI binary | `/usr/bin/qzypper` |
| Backend binary | `/usr/libexec/qzypper-backend` |
| D-Bus config | `/usr/share/dbus-1/system.d/org.presire.qzypper.conf` |
| D-Bus service | `/usr/share/dbus-1/system-services/org.presire.qzypper.service` |
| systemd unit | `/usr/lib/systemd/system/dbus-org.presire.qzypper.service` (default, controlled by `SYSTEMD_SYSTEM_UNIT_DIR`) |
| Polkit policy | `/usr/share/polkit-1/actions/org.presire.qzypper.policy` |
| Desktop entry | `/usr/share/applications/org.presire.qzypper.desktop` |
| App icon | `/usr/share/icons/hicolor/{64x64,128x128,256x256,512x512,1024x1024}/apps/qZypper.png` |
| SELinux policy | `/usr/share/selinux/packages/qzypper.pp` |

### SELinux Policy Installation

The SELinux files and the policy-load step belong to the `selinux` install component. When `cmake --install` runs as root, SELinux is enabled, and `semodule` and `restorecon` were found at configure time, the policy module is loaded (`semodule -i /usr/share/selinux/packages/qzypper.pp`) and the binaries are relabeled (`restorecon`). When `DESTDIR` is set (staging, as done by RPM packaging), the policy is not loaded or relabeled during install; the RPM `%post` script loads it instead. With `-DENABLE_SELINUX=OFF`, nothing SELinux-related is built or installed, and the RPM `%post`/`%postun` scripts are not registered.

## Directory Structure

```
qZypper/
├── src/
│   ├── common/                    # Shared types (PackageInfo, RepoInfo, Types)
│   ├── backend/                   # D-Bus backend service
│   │   ├── ZyppManager            #   libzypp wrapper (singleton)
│   │   ├── PackageManagerAdaptor  #   D-Bus adaptor
│   │   └── ZyppCallbackReceiver   #   Progress callbacks
│   └── gui/                       # GUI application
│       ├── controllers/           #   PackageController, DBusClient
│       └── qml/                   #   QML screens, dialogs, components
├── dbus/                          # D-Bus bus policy and service file
├── systemd/                       # systemd D-Bus activation unit
├── polkit/                        # Polkit action definitions
├── desktop/                       # Desktop entry template
├── selinux/                       # SELinux policy module
├── packaging/                     # RPM post-install/uninstall scripts
├── translations/                  # Japanese translation (.ts)
└── CMakeLists.txt                 # Top-level build configuration
```
