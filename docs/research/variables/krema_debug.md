# KremaDebug Variables

## Overview
A singleton for debugging geometry, PipeWire states, and preview controllers.

## Registry
| Name | Singleton? | Owner | Purpose | Consumers | Architectural Link |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `KremaDebug` | Yes | `DebugManager` | Debug logging interface | `PreviewThumbnail`, `Shell` | Rule 12 |
