# Configuration File

[日本語](CONFIGURATION_JP.md)  

qZypper stores user preferences in `~/.config/Presire/qZypper.conf` (INI format, managed by Qt's `Settings` type).  
This file is created automatically on first launch and updated when the application exits.  

## Saved Settings

| Category | Key | Description |
|---|---|---|
| `window` | `width`, `height` | Main window size |
| `splitView` | `leftPaneWidth`, `statusFilterHeight`, `detailPaneHeight` | Splitter positions (left pane, status filter, detail pane) |
| `statusFilter` | `mask` | Status filter selection in the package list |
| `theme` | `darkMode` | Dark mode (`bool`, default: system color scheme) |
| `packageTable` | `colName`, `colSummary`, `colInstalled`, `colAvailable` | Package table column widths |
| `repoTable` | `colPriority`, `colEnabled`, `colAutoRefresh`, `colName`, `colService` | Repository table column widths |
| `serviceTable` | `colEnabled`, `colName`, `colType` | Service table column widths |
| `refresh` | `onStartup` | `bool`, default `true`. Refresh stale repositories at startup |
| `solver` | `installRecommends` | `int`, default `-1`. Install recommended packages: `-1` follows `zypp.conf`, `0` is off, `1` is on |
| `packageFilter` | `hideDevel` | `bool`, default `true`. Hide `-devel` packages in package lists |
| `packageFilter` | `hideDebug` | `bool`, default `true`. Hide `-debuginfo` and `-debugsource` packages in package lists |
| `search` | `matchModeIndex` | `int`, default `0` (Auto). Match mode: `0` Auto, `1` Contains, `2` Starts With, `3` Exact Match, `4` Wildcards, `5` Regular Expression |
| `search` | `caseSensitive` | `bool`, default `false`. Match case |
| `search` | `searchName` | `bool`, default `true`. Search package names |
| `search` | `searchKeywords` | `bool`, default `true`. Search keywords |
| `search` | `searchSummary` | `bool`, default `true`. Search summaries |
| `search` | `searchDescription` | `bool`, default `false`. Search descriptions |
| `search` | `searchProvides` | `bool`, default `false`. Search RPM provides |
| `search` | `searchRequires` | `bool`, default `false`. Search RPM requires |
| `search` | `searchRecommends` | `bool`, default `false`. Search RPM recommends |
| `search` | `searchFileList` | `bool`, default `false`. Search file lists |

### Startup Refresh

When `refresh/onStartup` is `true`, qZypper asks the backend to refresh repository metadata after the session starts. Only enabled repositories with auto-refresh are considered, and a repository is fetched only when its metadata is stale. The `org.presire.qzypper.initialize` Polkit action implies `org.presire.qzypper.refresh-repos`, so the session start and this refresh need a single authentication.

### Solver Settings

Only `solver/installRecommends` is written back to the configuration file. The other solver toggles in the navigation drawer, "Clean Up Dependencies When Deleting" and "Allow Vendor Change", apply to the current session only. They are passed to the backend, but they are not saved and are discarded when the editing session is released or the backend exits. `solver/installRecommends` is updated only after the backend has applied the new value; if applying fails, an error is shown and the saved value stays unchanged. The drawer shows the current `zypp.conf` default for each option. "Allow Vendor Change" does not affect Distribution Upgrade, which sets its own option in the upgrade dialog.

### Package Filters

`packageFilter/hideDevel` and `packageFilter/hideDebug` control the initial state of the list filters. Rows that already have a pending change are never hidden by these filters, so a queued install, update, or delete stays visible until the transaction is committed.

### Search Settings

`search/matchModeIndex` stores the position of the match mode selector. Auto mode inspects the trimmed query before searching:

- Empty or whitespace-only input performs no search.
- A leading `=` selects Exact Match and searches for the remaining text. A lone `=` is detected as Exact Match, but because the remaining text is empty, no search is performed.
- A query containing any of `^ $ ( ) | [ ] { } \`, or the sequences `.*` or `.+`, selects Regular Expression.
- Otherwise, a query containing `*` or `?` selects Wildcards.
- Any other query selects Contains.

Regular expression detection runs before wildcard detection, and a standalone `+` is not treated as a regular expression character, so names such as `libstdc++` or `g++` remain Contains searches. The `search/*` keys that select search targets are booleans that enable each target; `search/matchModeIndex` is an integer.

All settings are optional. If the file is deleted, defaults are restored on next launch.  
