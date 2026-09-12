# TaskIconProvider Variables

## Overview
`TaskIconProvider` is responsible for fetching, normalizing, and scaling application icons for the dock. It ensures visual uniformity by processing raw icons to a consistent "content ratio" before rendering.

## Architectural Role
- **Source Resolver**: Bridges multiple icon sources: native executables (Steam/Direct .exe), KDE Theme icons, and `.desktop` file entries.
- **Visual Normalizer**: Analyzes icon content bounds (`findContentBounds`) to remove excess whitespace and shrink oversized icons so all dock items have a consistent visual weight.
- **Normalization Bypass**: If a provider fails (e.g., `image://taskicon/neshi-desktop-electron`), the UI falls back to a raw `QIcon` (via `model.decoration`), which bypasses all normalization math and leads to sizing discrepancies.

## Registry
| Name | Singleton? | Owner | Purpose | Consumers | Architectural Link |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `m_normalizationEnabled` | No | `TaskIconProvider` | Toggles normalization processing | `DockShell` | Performance |
| `m_indicatorOffset` | No | `TaskIconProvider` | Scaling factor for indicator | `DockShell` | UI/UX |
| `kAlphaThreshold` | No | `TaskIconProvider` | Content detection cutoff | `TaskIconProvider` | Geometry |
| `kMinContentRatio` | No | `TaskIconProvider` | Min threshold for processing | `TaskIconProvider` | Geometry |

## Normalization Stages
1. **Steam Hunter**: Scans Steam library paths to find native .exe files and extract icons.
2. **Direct .exe Catch**: Extracts icons directly from Windows binaries (using `PeIconExtractor`).
3. **Theme Check**: Fallback to standard `QIcon::fromTheme`.
4. **Desktop File Bridge**: Resolves `.desktop` entries if theme lookups fail.
5. **Normalization**: Calculates content-fill ratio and applies `shrinkPixmap` or `normalizePixmap` to ensure consistent visual size.
