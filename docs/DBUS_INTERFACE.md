# D-Bus Interface

[日本語](DBUS_INTERFACE_JP.md)  

- **Service**: `org.presire.qzypper`  
- **Object path**: `/org/presire/qzypper`  
- **Interface**: `org.presire.qzypper.PackageManager`  
- **Bus**: System bus  

## Backend Activation (systemd)

D-Bus activation delegates to a systemd service instead of starting the backend binary directly. The D-Bus service file sets `SystemdService=dbus-org.presire.qzypper.service`, so the system bus asks systemd to launch the unit.

The systemd unit (`systemd/dbus-org.presire.qzypper.service.in`) describes a root backend service:  

- `Type=dbus` with `BusName=org.presire.qzypper`: systemd considers the service started once the bus name is acquired.  
- `User=root`: the backend runs with root privileges for libzypp operations.  
- `ExecStart` points at `/usr/libexec/qzypper-backend`.  

The unit fixes the process environment and unsets inherited variables:  

- `Environment=HOME=/root` and a fixed `PATH` (`/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin`).  
- `UnsetEnvironment=` removes the dynamic loader variables (`LD_LIBRARY_PATH`, `LD_PRELOAD`, `LD_AUDIT`, `GLIBC_TUNABLES`), the XDG directory variables,  
  GnuPG variables (`GNUPGHOME`, `GPG_AGENT_INFO`), the session D-Bus address (`DBUS_SESSION_BUS_ADDRESS`), the Qt/QML import path variables,  
  and the libzypp override variables (`ZYPP_CONF`, `ZYPP_REPO_RELEASEVER`, `ZYPP_PLUGIN_*`).  

The backend therefore starts with a deterministic environment rather than inheriting the activating caller's.  

## Backend (`qzypper-backend`) Lifetime

The backend is auto-started by D-Bus activation. It has no explicit shutdown method; its lifetime is governed by an idle timer and by the session owner's connection:  

- A single-shot 5-minute idle timer starts when the backend starts.  
- The timer is not stopped when a worker operation starts; it is restarted when the session owner's operation finishes.  
- Calls from the session owner restart the timer.  
- When the timer fires, the backend exits if no operation is running and no session owner is connected. If an operation is running or an owner is connected, it re-arms and waits again.  
- If `Initialize` authorizations are still pending when the timer fires, the backend waits only for those requests to finish, and rejects new `Initialize` calls with `AccessDenied` in the meantime. It exits as soon as the last of them ends without producing an owner. Each authorization waits at most 5 minutes, so repeated authorization requests cannot keep the backend running indefinitely.  

While the GUI holds the session, the backend therefore never exits from inactivity, however long the GUI stays idle.  

When the session owner's D-Bus connection disappears (GUI exit or crash) or the owner calls `ReleaseSession` (End Editing Session), the backend does not hand the session to another client. It releases the owner session and restarts the 5-minute idle timer; the process then exits when that timer fires, not immediately.  

On normal GUI exit (`Ctrl` + `Q` → `Qt.quit()`) the GUI's bus connection closes, which triggers the rule above. If the backend exits or crashes while the GUI is running, the GUI shows the Session Ended dialog. Start Session restarts the backend through D-Bus activation and runs `Initialize()` again. If the service is registered again by another trigger, the GUI's `QDBusServiceWatcher` reconnects automatically: the `backendReconnected` signal triggers `Initialize()` and a repository reload.

## Authorization and Ownership

- On startup the GUI calls `Initialize()`. The backend asks Polkit (action `org.presire.qzypper.initialize`, admin authentication) for the caller's unique bus name (subject kind `system-bus-name`). Only if that authorization succeeds does the connection become the session **owner**, and libzypp is initialized.  
- Other clients receive `org.freedesktop.DBus.Error.AccessDenied` with the message "Another client owns the qZypper session".  
- Pending `Initialize` authorizations are bounded: one per connection, at most 2 per Unix user (UID, resolved via `org.freedesktop.DBus.GetConnectionUnixUser`), and at most 8 overall. Excess requests receive `org.freedesktop.DBus.Error.LimitsExceeded`.  
- Every privileged method is authorized server-side again with Polkit for the caller's bus name: `org.presire.qzypper.refresh-repos`, `org.presire.qzypper.manage-repos`, `org.presire.qzypper.install-packages`, and `org.presire.qzypper.trust-key`.  
- Mutating methods are owner-only. Only one operation runs at a time: a single worker thread executes it and the D-Bus reply is delayed until it finishes. Concurrent calls fail with `org.presire.qzypper.Error.Busy`. `CancelOperation()` (owner only) requests cooperative cancellation through a cancellation token.  
- `Commit(t expectedRevision)`: the GUI reads the current selection revision with `GetSelectionRevision()` and passes it back. Dependencies must have been resolved for that exact revision, otherwise the call is rejected with `org.presire.qzypper.Error.SelectionChanged`.  
- When an operation hits a repository signing key that is not yet trusted, the backend sends `PromptRequested(id, "gpg-key", data)` (with the key ID, fingerprint, owner, and dates) to the session owner. The owner answers with `AnswerPrompt(id, {"action": ...})`: `trust-temporary` trusts the key for the current operation only, `trust-permanent` imports it into the keyring, and `reject` refuses it. Both trust actions additionally require Polkit `trust-key`; while that authorization is pending, the prompt's 60-second timeout is paused, and a denied authorization rejects the key. An unanswered, expired, or invalid prompt rejects the key. If no prompt handler is available, the backend emits `UntrustedKeyDetected(a{sv})` and refuses the key; the user can then pre-approve that exact fingerprint with `TrustKey(s fingerprint)` (Polkit `trust-key`), and the key is imported once the next time libzypp asks for it. Signature problems (unsigned files, unknown key IDs, verification failures) and digest problems are always rejected without any prompt; there is no accept-once for them.  
- Progress and state signals (`ProgressChanged`, `CommitProgressChanged`, `RepoRefreshProgress`, `TransactionFinished`, `ErrorOccurred`, `PackageStateChanged`, `UntrustedKeyDetected`, `PromptRequested`, `PromptClosed`) are unicast: they are sent only to the session owner, not broadcast to the bus.  

## Polkit Actions

| Action ID | Operation | Default |
|---|---|---|
| `org.presire.qzypper.initialize` | Open a package management session (become session owner) | auth_admin (active: auth_admin_keep) |
| `org.presire.qzypper.refresh-repos` | Refresh repositories | auth_admin_keep |
| `org.presire.qzypper.manage-repos` | Add / remove / modify repos and services | auth_admin_keep |
| `org.presire.qzypper.install-packages` | Install / remove / update packages | auth_admin_keep |
| `org.presire.qzypper.trust-key` | Trust a new repository signing key | auth_admin |

## Methods

| Category | Method | Description |
|---|---|---|
| Initialization | `Initialize()` | Open the session (owner only) and initialize libzypp |
| Repository | `GetRepos()` | Get repository list |
| | `RefreshRepos()` | Refresh all repositories |
| | `RefreshSingleRepo(alias)` | Refresh a single repository |
| | `TrustKey(fingerprint)` | Approve a repository signing key (one-shot) |
| | `AnswerPrompt(id, answer)` | Answer an in-flight prompt such as a GPG key trust request (owner only) |
| | `AddRepo(url, name)` | Add repository (simple) |
| | `AddRepoFull(properties)` | Add repository (full properties) |
| | `RemoveRepo(alias)` | Remove repository |
| | `SetRepoEnabled(alias, enabled)` | Enable / disable repository |
| | `ModifyRepo(alias, properties)` | Modify repository properties |
| | `ParseRepoFile(content)` | Parse `.repo` file content (read-only, owner only) |
| Service | `GetServices()` | Get service list |
| | `AddService(url, alias)` | Add service |
| | `RemoveService(alias)` | Remove service |
| | `ModifyService(alias, properties)` | Modify service properties |
| | `RefreshService(alias)` | Refresh service |
| Package | `SearchPackages(query, flags)` | Literal, case-insensitive substring search (see below) |
| | `SearchPackagesEx(options)` | Extended search: substring, exact, glob, or POSIX ERE regex (see below) |
| | `GetPackageDetails(name)` | Get package details |
| | `GetPackagesByRepo(repoAlias)` | Get packages by repository |
| | `GetPatterns()` | Get pattern list |
| | `GetPackagesByPattern(patternName)` | Get packages by pattern |
| | `GetPatches(category)` | Get patch list |
| | `GetPendingChanges()` | Get pending changes |
| Status | `SetPackageStatus(name, status)` | Change package status |
| | `SetPackageVersion(name, version, arch, repoAlias)` | Select specific package version |
| | `SetPatternStatus(name, status)` | Change pattern status |
| Update | `UpdateAllPackages()` | Update all packages (doUpdate) |
| Solver | `ResolveDependencies()` | Run dependency resolver |
| | `ApplySolution(problemIndex, solutionIndex)` | Apply conflict resolution |
| Commit | `GetSelectionRevision()` | Get the current selection revision (owner only) |
| | `Commit(expectedRevision)` | Commit the confirmed selection revision |
| State | `SaveState()` | Save selection state |
| | `RestoreState()` | Restore selection state |
| Other | `GetDiskUsage()` | Get disk usage info |
| | `CancelOperation()` | Request cooperative cancellation (owner only) |

Privileged and mutating methods are subject to the authorization and owner rules described above. Read-only queries are owner-gated as well: every query requires that `Initialize()` has authorized the caller as the session owner, including `ParseRepoFile` and `GetSelectionRevision`.

## Repository and Package Operations

- `RefreshRepos()` continues refreshing the remaining repositories when one fails, and at the end returns a failure carrying the list of failed repositories. `RefreshSingleRepo(alias)` reports the failure of that repository.
- `ModifyRepo(alias, properties)` validates URL changes and checks the modified repository configuration against the repository policy before saving: the alias and name must not contain control characters, every base URL and GPG key URL must pass the URL validation below, a mirrorlist must use `https`, `file`, or `dir`, and service-originated repositories cannot inject mirrorlists or, while GPG checking is enabled in `zypp.conf`, weaken `gpgcheck`. A policy violation fails without changing the `.repo` file. Mirror endpoints are checked right before refresh or commit, not in `ModifyRepo`.
- `InstallLocalRpms(files, fileNames)` copies local RPMs on the backend and is cancellable during the copy as well; a cancelled call returns `org.presire.qzypper.Error.Cancelled`.
- Importing a `.repo` file accepts only regular files of 64 KiB or less: the GUI rejects anything that is not a regular file, and `ParseRepoFile` rejects content larger than 64 KiB.

## Package Search

`SearchPackages(query, flags)` performs literal (not regular-expression) matching: it returns packages whose text contains the query as an ASCII-case-insensitive substring. A query longer than 256 bytes is rejected with `org.freedesktop.DBus.Error.InvalidArgs`.

The `flags` bitmask (`SearchFlag`) selects which fields are searched; all flags are honoured:

| Flag | Value | Field |
|---|---|---|
| `Name` | 1 | Package name |
| `Keywords` | 2 | Package keywords |
| `Summary` | 4 | Summary |
| `Description` | 8 | Description |
| `Requires` | 16 | Requires dependencies |
| `Provides` | 32 | Provides dependencies |
| `FileList` | 64 | File list (only files available in the loaded metadata / installed packages) |

`SearchPackagesEx(options)` is an extended variant driven by an option map. Its `match` field selects the mode: `substring` (the default, ASCII-case-insensitive), `exact`, `glob`, or `regex`. `regex` is POSIX ERE, validated at the D-Bus boundary with the same flags used by libzypp (`StrMatcher`) and libsolv (`datamatcher`): `REG_EXTENDED | REG_NOSUB | REG_NEWLINE | REG_ICASE`. To bound compilation cost, patterns using back-references, nested or consecutive repetition, or a total of `{m,n}` upper bounds above 1,000 are rejected with `org.freedesktop.DBus.Error.InvalidArgs`. Both search methods reject a query longer than 256 bytes.

## Repository URL Validation

Repository URLs are validated before use. The scheme must be one of `http`, `https`, `ftp`, `tftp`, `file`, `dir`, `hd`, `iso`, `cd`, `dvd`, `nfs`, `nfs4`, `smb`, or `cifs` (`plugin` and other schemes are rejected). For `iso` URLs, a nested `url` query parameter is validated recursively. Repository aliases allow at most 200 bytes, must not start with `.`, and must not contain `/`, `\`, `[`, `]`, or control characters. Display names allow at most 1024 bytes and must not contain control characters.

## Operation History

`GetHistory(options)` returns the operation history (newest first) read from the libzypp history file. `offset` and `limit` are validated so that `offset + limit` does not exceed 1,000,000, the history file is rejected if it exceeds 64 MiB, and it is also rejected if the number of valid lines scanned exceeds 1,000,000. Exceeding any of these limits returns an error, and a history read failure returns an error (`org.presire.qzypper.Error.Cancelled` when the operation was cancelled, otherwise `org.freedesktop.DBus.Error.Failed`). A missing history file yields an empty result.

## Signals

| Signal | Description |
|---|---|
| `ProgressChanged(packageName, percentage, stage)` | Operation progress |
| `CommitProgressChanged(packageName, percentage, stage, totalSteps, completedSteps, overallPercentage)` | Detailed commit progress |
| `TransactionFinished(success, summary)` | Transaction completed |
| `RepoRefreshProgress(repoAlias, percentage)` | Repository refresh progress |
| `ErrorOccurred(errorMessage)` | Error notification |
| `PackageStateChanged(packageName, event)` | Package state transition during commit |
| `UntrustedKeyDetected(keyInfo)` | Untrusted signing key detected (`a{sv}` including the fingerprint) |
| `PromptRequested(id, type, data)` | In-flight prompt that needs an answer via `AnswerPrompt` (e.g. `gpg-key`) |
| `PromptClosed(id)` | Prompt ended (answered, expired, or interrupted) |

These signals are unicast: they are sent only to the session owner, not broadcast to all bus clients.
