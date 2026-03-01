# kwin-mcp: Blur/Contrast visual effect rendering verification

## Summary

When running applications inside kwin-mcp's isolated KWin session, we need to verify whether `KWindowEffects::enableBlurBehind()` and `enableBackgroundContrast()` produce **visible visual effects** in screenshots.

## Environment

- KDE Plasma 6.5.5
- KWin (isolated session via kwin-mcp)
- KDE Frameworks 6

## Current Status

### Blur Effect

D-Bus queries confirm:
```
isEffectLoaded("blur")    → true
isEffectSupported("blur") → true
```

Wayland protocol available:
```
org_kde_kwin_blur_manager, version: 1
```

**Question**: Even though blur is loaded and supported, does it actually render a visible Gaussian blur in the virtual framebuffer? When capturing a screenshot with `screenshot()`, does the blur effect appear in the image?

This is important for testing dock applications (like Krema) that use blur as part of their visual design. If blur is loaded but doesn't produce visible output in screenshots, we cannot visually verify blur-dependent features.

### Contrast Effect

D-Bus queries show:
```
isEffectLoaded("contrast")    → false
isEffectSupported("contrast") → (not tested)
loadEffect("contrast")        → false (load failed)
```

Wayland protocol is available:
```
org_kde_kwin_contrast_manager, version: 2
```

**The contrast effect plugin is NOT loaded** and **cannot be loaded** via D-Bus in the isolated session. This means `KWindowEffects::enableBackgroundContrast()` calls are effectively no-ops.

## Impact

1. **Blur**: Need to confirm whether blur renders visually. If yes, blur-dependent styles can be tested. If no, this is a visual testing limitation.

2. **Contrast**: Cannot be loaded at all. Applications using `enableBackgroundContrast()` for panel-style backgrounds (e.g., Plasma panel inherit) cannot be accurately tested. The contrast overlay that normally provides the "frosted glass" panel look will be missing.

## Suggested Improvements

### For contrast effect

1. Include the contrast effect plugin in the isolated KWin session's plugin configuration:
   ```ini
   [Plugins]
   contrastEnabled=true
   ```

2. Or load it during session initialization:
   ```python
   # In session_start(), after KWin is ready:
   dbus_call("org.kde.KWin", "/Effects", "org.kde.kwin.Effects", "loadEffect", ["string:contrast"])
   ```

3. If loading fails due to missing plugin binary, document which KDE package provides it (likely `kwin` itself or `kwin-effects`).

### For blur verification

Add a test case to kwin-mcp's test suite that:
1. Launches a window with `enableBlurBehind()`
2. Places a colorful window behind it
3. Takes a screenshot and verifies the blur is visible (the background should appear blurred, not sharp)
