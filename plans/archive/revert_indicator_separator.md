# Plan: Revert Indicator and Separator Changes

## 1. Objective
Revert the recent experimental changes that made indicator sizing and separator dimensions user-customizable. These changes did not work as intended.

## 2. Proposed Changes
- **`src/config/krema.kcfg`**: Remove `IndicatorSize` entry.
- **`src/qml/settings/IconsPage.qml`**: Remove the `Indicator Size` slider.
- **`src/qml/settings/SeparatorPage.qml`**: Remove the `SeparatorWidth` and `SeparatorLength` sliders.
- **`src/qml/AppIcon.qml`**: Revert `_dotHeight` to the hardcoded `4` pixels.
- **`src/qml/main.qml`**: Revert `pinnedSeparator` width and height to use strict proportional math (`autoThickness` and `autoLength`).

## 3. Execution
- Use `replace` to execute the changes across all 5 files.