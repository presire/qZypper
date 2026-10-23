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
| `org.presire.qzypper.manage-locks` | Add / remove package locks | auth_admin_keep |
| `org.presire.qzypper.trust-key` | Trust a new repository signing key | auth_admin |

The `org.presire.qzypper.initialize` action carries the annotation `org.freedesktop.policykit.imply` = `org.presire.qzypper.refresh-repos`. The session authorization is retained with `auth_admin_keep`, so the automatic repository refresh at startup does not ask for a password a second time. The default values of the other actions are unchanged.

## Methods

| Category | Method | Description |
|---|---|---|
| Initialization | `Initialize()` | Open the session (owner only) and initialize libzypp |
| | `ReleaseSession() → b` | End the owner's session and discard the uncommitted selection (fails with `org.presire.qzypper.Error.Busy` while an operation runs) |
| Repository | `GetRepos()` | Get repository list |
| | `RefreshRepos()` | Refresh all repositories |
| | `RefreshReposIfNeeded() → b` | Refresh only stale, enabled, auto-refresh repositories (Polkit `org.presire.qzypper.refresh-repos`) |
| | `RefreshSingleRepo(alias)` | Refresh a single repository |
| | `TrustKey(fingerprint)` | Approve a repository signing key (one-shot) |
| | `AnswerPrompt(id, answer)` | Answer an in-flight prompt such as a GPG key trust request (owner only) |
| | `AddRepo(url, name)` | Add repository (simple) |
| | `AddRepoFull(properties)` | Add repository (full properties) |
| | `RemoveRepo(alias)` | Remove repository |
| | `SetRepoEnabled(alias, enabled)` | Enable / disable repository |
| | `ModifyRepo(alias, properties)` | Modify repository properties |
| | `ParseRepoFile(content)` | Parse `.repo` file content (read-only, owner only) |
| | `ImportRepoFile(s content) → a{sv}` | Add the repositories defined in `.repo` content (Polkit `org.presire.qzypper.manage-repos`) |
| | `GetExpandedRepoUrl(s rawUrl) → s` | Preview a raw URL with repository variables expanded (read; see below) |
| | `GetRepoPresets() → aa{sv}` | Community repository presets for the running distribution (read; see below) |
| Service | `GetServices()` | Get service list |
| | `AddService(url, alias)` | Add service |
| | `RemoveService(alias)` | Remove service |
| | `ModifyService(alias, properties)` | Modify service properties |
| | `RefreshService(alias)` | Refresh service |
| Package | `SearchPackages(query, flags)` | Literal, case-insensitive substring search (see below) |
| | `SearchPackagesEx(options)` | Extended search: substring, prefix, exact, glob, or POSIX ERE regex (see below) |
| | `GetPackageDetails(name)` | Get package details |
| | `GetPackagesByRepo(repoAlias)` | Get packages by repository |
| | `GetProducts() → aa{sv}` | Get the product list (read; see below) |
| | `GetPatterns()` | Get pattern list |
| | `GetPackagesByPattern(patternName)` | Get packages by pattern |
| | `GetPatches(category)` | Get patch list (see Patch Categories) |
| | `GetPendingChanges()` | Get pending changes |
| | `GetPackagesByClassification(s classification) → aa{sv}` | Get packages in a classification such as `recommended` or `updatable` (read; see below) |
| | `GetRpmGroups() → aa{sv}` | Get the RPM group tree with package counts (read) |
| | `GetPackagesByRpmGroup(s path) → aa{sv}` | Get packages whose RPM group is `path` or below it (read) |
| | `GetLocales() → aa{sv}` | Get available and requested locales (read) |
| | `GetPackagesByLocale(s code) → aa{sv}` | Get packages for a locale (read) |
| | `GetPendingLocaleChanges() → aa{sv}` | Get locale changes not yet committed (read) |
| Status | `SetPackageStatus(name, status)` | Change package status |
| | `SetPackageVersion(name, version, arch, repoAlias)` | Select specific package version |
| | `SetPackageVersionStatus(s name, s version, s arch, s repoAlias, i status) → b` | Install or remove one version of a multiversion package (owner only; see below) |
| | `SetPatternStatus(name, status)` | Change pattern status |
| | `SetPackagesAction(as names, s action) → a{sv}` | Apply one action to many packages (owner only; see below) |
| | `SelectSubpackages(s suffix) → a{sv}` | Select `-devel`, `-debuginfo`, or `-debugsource` subpackages of installed packages (owner only) |
| | `SelectAllRecommended() → a{sv}` | Select all recommended packages and resolve (owner only) |
| | `SetLocaleRequested(s code, b requested) → b` | Mark a locale as requested or not (owner only) |
| | `SetPatchStatus(s id, i status) → b` | Change patch status (owner only) |
| | `SelectAllNeededPatches(a{sv} options) → a{sv}` | Select all needed patches (owner only) |
| Update | `UpdateAllPackages()` | Update all packages (doUpdate) |
| | `DistUpgrade(a{sv} options) → a{sv}` | Prepare a distribution upgrade (owner only) |
| | `GetTransactionSummary() → a{sv}` | Summary of the resolved transaction (owner only) |
| Solver | `ResolveDependencies()` | Run dependency resolver |
| | `ApplySolution(problemIndex, solutionIndex)` | Apply conflict resolution |
| | `GetSolverSettings() → a{sv}` | Get the effective solver settings and the `zypp.conf` defaults (read; see below) |
| | `SetSolverSettings(a{sv} settings) → b` | Override solver settings for this backend session (owner only) |
| Commit | `GetSelectionRevision()` | Get the current selection revision (owner only) |
| | `ConfirmLicenses() → a{sv}` | Ask the owner to accept or reject pending licenses before committing (owner only; see below) |
| | `Commit(expectedRevision)` | Commit the confirmed selection revision |
| | `InstallLocalRpms(ah files, as fileNames) → a{sv}` | Install local RPM files passed as file descriptors (Polkit `org.presire.qzypper.install-packages`) |
| State | `SaveState()` | Save selection state |
| | `RestoreState()` | Restore selection state |
| Other | `GetDiskUsage()` | Get disk usage info |
| | `GetLocks() → aa{sv}` | Get package locks (read) |
| | `AddLock(a{sv} lock) → b` / `RemoveLock(a{sv} lock) → b` | Add / remove a package lock (Polkit `org.presire.qzypper.manage-locks`) |
| | `GetHistory(a{sv} options) → aa{sv}` | Get the flat operation history (see below) |
| | `GetHistoryGroups(a{sv} options) → a{sv}` | Get the operation history grouped by command (see below) |
| | `VerifySystem() → a{sv}` | Check dependencies of the installed system (owner only) |
| | `CreateSolverTestcase() → s` | Write a solver testcase and return its path (owner only) |
| | `GetOperationState() → a{sv}` | Get the current operation state |
| | `CancelOperation()` | Request cooperative cancellation (owner only) |

In this document `aa{sv}` means an array of maps. On the bus these values are sent as `av` whose elements are `a{sv}`; the exact signatures are in `src/backend/dbus/org.presire.qzypper.xml`.

Privileged and mutating methods are subject to the authorization and owner rules described above. Read-only queries are owner-gated as well: every query requires that `Initialize()` has authorized the caller as the session owner, including `ParseRepoFile` and `GetSelectionRevision`.

## Products

`GetProducts()` returns one map per product in the pool, sorted by name. An installed product uses the installed object's data; `available` tells whether a candidate exists. The list is read-only.

| Key | Type | Meaning |
|---|---|---|
| `name` | `s` | Product name |
| `shortName` | `s` | Short name |
| `summary` | `s` | Summary |
| `version` | `s` | Version of the shown object |
| `installedVersion` | `s` | Installed version (empty if not installed) |
| `vendor` | `s` | Vendor |
| `arch` | `s` | Architecture |
| `repoAlias` | `s` | Repository alias |
| `repoName` | `s` | Repository name |
| `status` | `i` | Status (`PackageStatus`) |
| `installed` | `b` | Installed |
| `available` | `b` | A candidate is available |
| `isBase` | `b` | Installed base product (target distribution) |

## Patch Categories

`GetPatches(category)` accepts these values. `-1` and `4` return every category.

| Value | Category |
|---|---|
| `0` | `security` |
| `1` | `recommended` |
| `2` | `optional` |
| `3` | `feature` |
| `4` | All |
| `5` | `document` |
| `6` | `yast` |

## Multiversion Packages and Technical Data

`GetPackageDetails(name)` includes `buildTime`, `installTime` (ISO 8601 local time; empty when unknown or not installed), `installSize`, `downloadSize` (bytes), `multiversion`, and `mixedMultiversion`. Each entry of `availableVersions` comes from the selectable's pick list (available versions and installed versions without an identical available one) and has `version`, `arch`, `repoAlias`, `repoName`, `installed`, `status` (pick status), `multiversion`, `size`, `downloadSize`, `buildTime`, and `installTime`.

`SetPackageVersionStatus(name, version, arch, repoAlias, status)` changes the pick status of one version of a multiversion package. Only `Install`, `NoInst`, `KeepInstalled`, and `Del` are accepted. The call fails for unknown, ambiguous, or non-multiversion entries. `GetPendingChanges()` and `GetTransactionSummary()` list multiversion packages per version.

## License Confirmation

`ConfirmLicenses()` runs after a successful `ResolveDependencies()` and before `Commit()`. It sends a `license` prompt for each package whose license still needs confirmation.

- Accept: the approval is bound to the package name, edition, architecture, repository alias, and the SHA-256 of the license text.
- Reject: a package to be installed becomes `Taboo`, a package to be updated becomes `Protected`, the selection revision changes, and the client must resolve and call `ConfirmLicenses()` again.

| Result key | Type | Meaning |
|---|---|---|
| `success` | `b` | The check finished without error |
| `changed` | `b` | At least one license was rejected and the selection changed |
| `rejectedPackages` | `as` | Names of the rejected packages |
| `acceptedCount` | `i` | Number of accepted licenses |
| `errorMessage` | `s` | Error text |

`Commit()` fails before any change if a license that was not confirmed remains.

## Commit Problem Prompts

When downloading, installing, or removing a package fails during `Commit()`, the backend sends a `PromptRequested` signal with the type `download-error`, `install-error`, or `remove-error`. Its data contains `message`, `details`, `operation`, `package`, `errorKind`, `actions`, and `buttons`. The answer is `{"action": "abort" | "retry" | "ignore"}`; `ignore` is not offered for verification errors of downloads. Download I/O errors are retried automatically 3 times before the prompt appears. An ignored package is reported in `failedPackages` and `ignoredPackages` of the commit result and never counts as success. The safe default is `abort`.

## Repository Variables and Presets

`GetExpandedRepoUrl(rawUrl)` expands repository variables such as `$releasever`, `$basearch`, and `$arch` with libzypp. A password in the URL is replaced with `***`. It returns an empty string when the URL has no variables or does not change. URLs longer than 2048 bytes or with control characters are rejected with `org.freedesktop.DBus.Error.InvalidArgs`. Repositories are always stored with the raw URL.

`GetRepoPresets()` returns third-party community repositories for openSUSE Tumbleweed and openSUSE Leap 16 (empty for other distributions). Each map has `id`, `name`, `alias`, `url`, `priority`, `autoRefresh`, `enabled`, `thirdParty`, and `description`. Adding a preset goes through the normal `AddRepoFull()` validation, Polkit check, and key trust.

## Plugin Services

Plugin services are discovered by libzypp from `/usr/lib/zypp/plugins/services/` and are listed by `GetServices()` with the type `plugin`. They cannot be added, removed, or modified. `RefreshService()` runs a plugin only when the script path contains no symbolic links, the script is a regular, root-owned file that is not writable by group or others, and every parent directory is root-owned and not writable by group or others.

## Package Classifications

`GetPackagesByClassification(classification)` accepts the values below. Matching is case-insensitive and surrounding whitespace is ignored. Any other value is rejected with `org.freedesktop.DBus.Error.InvalidArgs`.

| Classification | Meaning |
|---|---|
| `recommended` | Packages recommended by the solver result |
| `suggested` | Packages suggested by the solver result |
| `orphaned` | Packages detected as orphaned |
| `unneeded` | Packages detected as unneeded |
| `multiversion` | Packages with multiversion install enabled |
| `retracted` | Retracted packages |
| `retractedinstalled` | Retracted packages that are installed |
| `ptf` | Packages belonging to a program temporary fix (PTF) |
| `locked` | Packages whose status is taboo |
| `updatable` | Installed packages with an update candidate under libzypp's policy (vendor and architecture), excluding Protected packages |

`recommended`, `suggested`, `orphaned`, and `unneeded` are derived from the solver result, so the pool is resolved before the list is built. `updatable` is the set that Update All would update.

## Batch Package Operations

`SetPackagesAction(names, action)` applies the same action to every name in `names` and returns a result map. The accepted actions are `install`, `remove`, `update`, `updateForce`, `keep`, `taboo`, and `protect`. Duplicate names are processed once. Empty `names` or an unknown action is rejected with `org.freedesktop.DBus.Error.InvalidArgs`.

Protected and taboo packages are skipped (counted in `skipped`) for every action except `keep`.

| Result key | Type | Meaning |
|---|---|---|
| `changed` | `i` | Packages whose status or candidate changed |
| `skipped` | `i` | Packages left unchanged |
| `failedCount` | `i` | Total number of failed packages |
| `failed` | `as` | Names of failed packages, first 100 only; `failedCount` is the full total |

`SelectSubpackages(suffix)` selects the matching subpackages of installed packages. `suffix` must be `-devel`, `-debuginfo`, or `-debugsource`; any other value is rejected with `org.freedesktop.DBus.Error.InvalidArgs`. It returns the same result map as `SetPackagesAction`.

`SelectAllRecommended()` selects all recommended packages and returns the solver result map, in the same shape as `ResolveDependencies`: `success` (`b`), `problems` (`aa{sv}`), and `summary` (`a{sv}`). It records the resolved revision, so a `Commit` can follow without a separate resolve.

## Solver Settings

`GetSolverSettings()` returns the effective settings together with the `zypp.conf` defaults:

| Key | Type | Meaning |
|---|---|---|
| `installRecommends` | `b` | Install recommended packages |
| `cleandepsOnRemove` | `b` | Remove unneeded dependencies when removing packages |
| `allowVendorChange` | `b` | Allow changing the vendor of a package |
| `defaults` | `a{sv}` | The same three keys with their `zypp.conf` values |

`SetSolverSettings(settings)` accepts only the three keys above with boolean values. An empty map, an unknown key, or a non-boolean value is rejected with `org.freedesktop.DBus.Error.InvalidArgs`. Only the keys present in the map are overridden; the others keep their current value. The overrides are applied to the resolver immediately, on every resolve path, including classifications that run the solver. They advance the selection revision, so a `Commit` based on an earlier resolution is rejected with `org.presire.qzypper.Error.SelectionChanged`. It returns `true` when the overrides were applied. If applying them to the resolver fails, the stored overrides are restored to their previous values, the backend tries (best effort) to restore the resolver to those values, and the call fails with `org.freedesktop.DBus.Error.Failed`; the restore itself can fail, so the operation is not guaranteed to be atomic. The overrides are kept only for the backend editing session and are discarded when the session is released (`ReleaseSession`) or the backend shuts down. Persisting `installRecommends` is the GUI's job (see [Configuration File](CONFIGURATION.md)); the backend does not write `zypp.conf`.

## RPM Groups and Locales

`GetRpmGroups()` returns tree nodes with these keys: `path` (`s`, full group path), `label` (`s`, last path component), `depth` (`i`, number of `/` characters), and `count` (`i`, packages at or below that path). Nodes are sorted by `path`; `Unspecified` (packages without a group) is last and is omitted when no package lacks a group.

`GetPackagesByRpmGroup(path)` returns packages whose group equals `path` or starts with `path/`. The path `Unspecified` returns packages without a group. An empty path or one longer than 256 UTF-8 bytes is rejected with `org.freedesktop.DBus.Error.InvalidArgs`.

`GetLocales()` returns entries with `code` (`s`), `name` (`s`), `requested` (`b`), and `available` (`b`), sorted by code. `GetPackagesByLocale(code)` returns the packages for one locale and rejects an empty code or one longer than 256 UTF-8 bytes with `org.freedesktop.DBus.Error.InvalidArgs`.

`GetPendingLocaleChanges()` returns entries with `code` (`s`), `name` (`s`), and `action` (`s`, either `add` or `remove`), sorted by code. `SetLocaleRequested(code, requested)` marks a locale as requested or not; the change stays pending until it is committed. It rejects an empty code or one longer than 256 UTF-8 bytes with `org.freedesktop.DBus.Error.InvalidArgs`.

## Repository and Package Operations

- `RefreshRepos()` continues refreshing the remaining repositories when one fails, and at the end returns a failure carrying the list of failed repositories. `RefreshSingleRepo(alias)` reports the failure of that repository.
- `RefreshReposIfNeeded()` refreshes only enabled repositories with auto-refresh that are older than libzypp's refresh delay (the `RefreshIfNeeded` policy). It requires the Polkit action `org.presire.qzypper.refresh-repos` and emits the same `RepoRefreshProgress` signal as `RefreshRepos`.
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
| `Recommends` | 128 | RPM Recommends |

The `flags` value must be between 0 and 255 for `SearchPackages`; 255 enables every flag, including `Recommends`. Negative values or bits above 255 are rejected with `org.freedesktop.DBus.Error.InvalidArgs`.

`SearchPackagesEx(options)` is an extended variant driven by an option map. Its `match` field selects the mode: `substring` (the default, ASCII-case-insensitive), `prefix` (starts-with), `exact`, `glob`, or `regex`. `prefix` is implemented as an anchored POSIX ERE with the query escaped, so the query is matched literally from the start of the field. The `caseSensitive` option (`bool`, default `false`) makes matching case-sensitive. The `flags` option accepts 1 through 255. `regex` is POSIX ERE, validated at the D-Bus boundary with the same flags used by libzypp (`StrMatcher`) and libsolv (`datamatcher`): `REG_EXTENDED | REG_NOSUB | REG_NEWLINE | REG_ICASE`. To bound compilation cost, patterns using back-references, nested or consecutive repetition, or a total of `{m,n}` upper bounds above 1,000 are rejected with `org.freedesktop.DBus.Error.InvalidArgs`. Both search methods reject a query longer than 256 bytes.

## Package and Repository Info Maps

Methods that return package information (`SearchPackages`, `SearchPackagesEx`, `GetPackagesByRepo`, `GetPackagesByPattern`, `GetPackagesByClassification`, `GetPackagesByRpmGroup`, `GetPackagesByLocale`, and `GetPendingChanges`) return `aa{sv}` entries with these keys:

| Key | Type | Meaning |
|---|---|---|
| `name` | `s` | Package name |
| `summary` | `s` | Summary |
| `version` | `s` | Candidate version |
| `installedVersion` | `s` | Installed version |
| `arch` | `s` | Architecture |
| `repoAlias` | `s` | Repository alias |
| `repoName` | `s` | Repository name |
| `size` | `t` | Install size in bytes |
| `status` | `i` | Package status (`PackageStatus`) |
| `updatable` | `b` | Installed package with a newer update candidate |

`updatable` was added without changing any method signature or the D-Bus XML. A client talking to an older backend does not receive the key; treat a missing key as `false`.

`GetRepos()` returns `aa{sv}` entries with these keys:

| Key | Type | Meaning |
|---|---|---|
| `alias` | `s` | Repository alias |
| `name` | `s` | Display name |
| `url` | `s` | Base URL with variables expanded |
| `rawUrl` | `s` | Raw base URL, including variables such as `${releasever}` |
| `enabled` | `b` | Enabled |
| `autoRefresh` | `b` | Auto-refresh enabled |
| `priority` | `i` | Priority (1 to 99, lower is higher) |
| `type` | `s` | Repository type such as `rpm-md` or `yast2` |
| `keepPackages` | `b` | Keep the package cache |
| `service` | `s` | Alias of the owning service |
| `lastRefresh` | `x` | Last metadata refresh in epoch seconds; 0 means unknown |

## Repository URL Validation

Repository URLs are validated before use. The scheme must be one of `http`, `https`, `ftp`, `tftp`, `file`, `dir`, `hd`, `iso`, `cd`, `dvd`, `nfs`, `nfs4`, `smb`, or `cifs` (`plugin` and other schemes are rejected). For `iso` URLs, a nested `url` query parameter is validated recursively. Repository aliases allow at most 200 bytes, must not start with `.`, and must not contain `/`, `\`, `[`, `]`, or control characters. Display names allow at most 1024 bytes and must not contain control characters.

## Operation History

`GetHistory(options)` returns the operation history (newest first) read from the libzypp history file. `offset` and `limit` are validated so that `offset + limit` does not exceed 1,000,000, the history file is rejected if it exceeds 64 MiB, and it is also rejected if the number of valid lines scanned exceeds 1,000,000. Exceeding any of these limits returns an error, and a history read failure returns an error (`org.presire.qzypper.Error.Cancelled` when the operation was cancelled, otherwise `org.freedesktop.DBus.Error.Failed`). A missing history file yields an empty result.

## Grouped History

`GetHistoryGroups(options)` returns the history grouped by the `command` line that started each operation (newest first). Paging works on groups. Options: `limit` (1 to 1000 groups, default 100), `offset`, `text` (at most 256 bytes), `since` and `until` (epoch milliseconds), and `actions` (filters child events only). Each group has `id`, `seq`, `timestamp` (epoch seconds), `command`, `requestedBy`, `userdata`, `artificial` (events before the first command), `eventCount`, `installCount`, `removeCount`, `hiddenCount`, `truncated`, and `events`. The result has `groups`, `totalGroups`, `offset`, `limit`, `hiddenEvents`, `truncated`, and `hasMore`. A group keeps at most 500 events and a response at most 5000 events; extra events are counted in `hiddenCount` / `hiddenEvents`. The limits of `GetHistory` (64 MiB, 1,000,000 lines) also apply.

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
| `PromptRequested(id, type, data)` | In-flight prompt that needs an answer via `AnswerPrompt` (e.g. `gpg-key`, `license`, `media-change`, `file-conflicts`, `download-error`, `install-error`, `remove-error`) |
| `PromptClosed(id)` | Prompt ended (answered, expired, or interrupted) |
| `OperationStateChanged(state)` | Operation state changed (`a{sv}`, same keys as `GetOperationState()`) |
| `ScriptOutput(output)` | Output of RPM scripts during commit (`a{sv}`) |
| `PoolReloaded()` | The package pool was reloaded; clients must reload lists |

These signals are unicast: they are sent only to the session owner, not broadcast to all bus clients.
