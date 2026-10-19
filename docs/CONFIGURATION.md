# Configuration File

[日本語](CONFIGURATION_JP.md)  

qZypper stores user preferences in `~/.config/Presire/qZypper.conf` (INI format, managed by Qt's `Settings` type).  
This file is created automatically on first launch and updated when the application exits.  

## Saved Settings

| Category | Key | Description |
|---|---|---|
| `window` | `width`, `height` | Main window size |
| `splitView` | `leftPaneWidth`, `detailPaneHeight` | Splitter positions (left pane and detail pane) |
| `packageTable` | `colName`, `colSummary`, `colInstalled`, `colAvailable` | Package table column widths |
| `repoTable` | `colPriority`, `colEnabled`, `colAutoRefresh`, `colName`, `colService` | Repository table column widths |
| `serviceTable` | `colEnabled`, `colName`, `colType` | Service table column widths |

All settings are optional. If the file is deleted, defaults are restored on next launch.  
