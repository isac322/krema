# Universal Attention Engine Specification

> **Source of Truth:** [ARCHITECTURE Mandate.md](../../ARCHITECTURE%20Mandate.md) (Rule 16: Kinetic Physics)
> **Hierarchy:** Tier 3 (Item) Overlay

## 1. Urgency Level Definition
To ensure a consistent interaction language across Apps, Widgets, and Media, the dock maps all external signals to a internal four-level urgency state.

| Level | Urgency | Context | Example | Default Visual |
|---|---|---|---|---|
| 0 | **Idle** | Standard state | No messages | Standard Icon |
| 1 | **Low** | Informational | Track change, Download | Static Dot / Fade |
| 2 | **Normal** | Active | Discord DM, Email | Badge + Pulse |
| 3 | **Critical** | Urgent | 5% Battery, VOIP Call | Badge + Bounce/Glow |

## 2. Animation Logic (Kinetic Physics)
All animations MUST follow the **Rule 16 (Kinetic Physics)** mandate: smoothed easing curves, no rigid transitions.

### A. The Pulse (Badge Only)
- **Behavior:** Subtle scale oscillation (1.0 to 1.2) of the notification badge.
- **Trigger:** New notifications at Level 2.

### B. The Glow (Icon Outer)
- **Behavior:** Color-matched shadow pulse behind the icon portion.
- **Trigger:** System alerts or critical app states (Level 3).

### C. The Bounce (Icon Kinetic)
- **Behavior:** Vertical jump (approx 15% of icon height) with gravity-weighted easing.
- **Trigger:** High-urgency interaction requests (Level 3).

### D. The Shake (Icon Horizontal)
- **Behavior:** Subtle horizontal nudge (±2px).
- **Trigger:** Alternative for minimalist setups (Level 2 or 3).

## 3. Platform Triggers

### KWin (Plasma 6)
- **Apps:** Subscribes to `LibTaskManager`'s `needsAttention` and `launcherBadge` signals.
- **System:** Internal `Krema::SystemMonitor` bridge for Battery/Volume states.
- **Media:** `MPRIS` DBus interface listener.

### Hyprland
- **Apps:** Monitors `com.canonical.Unity.LauncherEntry` and workspace "urgent" window flags.
- **System:** Direct monitoring of `/sys/class/power_supply` or DBus UPower.
- **Media:** `MPRIS` DBus interface listener.

## 4. User Configuration (KConfigXT)
Users can map any animation to Levels 1-3.
- `UrgencyLevel1Animation`: [Static, Pulse, Glow, Bounce, Shake]
- `UrgencyLevel2Animation`: [Static, Pulse, Glow, Bounce, Shake]
- `UrgencyLevel3Animation`: [Static, Pulse, Glow, Bounce, Shake]
- `BadgeStyle`: [Dot, Number, Hidden]
