# Shadow Stack Variables

## Overview
Variables managing the dock's visual depth, shadow opacity, and clipping boundaries.

## Architectural Role
Shadows in Krema provide the perception of depth, separating the dock from the workspace wallpaper. These variables are managed by `DockView` to ensure shadows are never clipped by the dock surface edge and that the dock's elevation remains consistent across different themes and styles.

## Registry
| Name | Owner | Purpose | Consumers | Architectural Link |
| :--- | :--- | :--- | :--- | :--- |
| `margin` | `DockView` | Buffer for shadow clipping | `DockView` | Rule 12 |
| `elevation` | `DockView` | Dock height above wallpaper | `DockView` | Rule 12 |
| `shadowA` | `DockView` | Drop shadow opacity | `DockView` | Rule 12 |

## Mathematical Intent
These variables prevent "Shadow Suffocation" (Rule 12). By enforcing a specific buffer (`margin`), they guarantee that the GPU shader rendering the shadow has sufficient space to render the full falloff of the effect, ensuring the dock appears to "float" cleanly.
