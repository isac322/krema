# PipeWire Thumbnails (KPipeWire)

> Source: `/usr/include/KPipeWire/` headers

## Overview

KPipeWire provides Qt Quick integration for PipeWire streams. For a dock, the primary use case is rendering live window thumbnails via `PipeWireSourceItem`.

---

## PipeWireSourceItem

QML-ready item that renders a PipeWire stream.

**QML Element:** Yes (available directly in QML)
**Base:** `QQuickItem`

### Properties

| Property | Type | R/W | Description |
|----------|------|-----|-------------|
| `nodeId` | uint | RW | PipeWire node ID to render |
| `fd` | uint | RW | File descriptor for PipeWire connection |
| `state` | StreamState | R | Current stream state |
| `streamSize` | QSize | R | Source dimensions (updated after first frame) |
| `ready` | bool | R | True after first frame received |
| `allowDmaBuf` | bool | RW | Enable DMA-buffer GPU transfers |
| `usingDmaBuf` | bool | R | Whether currently using DMA-buf |

### StreamState Enum

```cpp
enum StreamState {
    Error,
    Unconnected,
    Connecting,
    Paused,
    Streaming,
};
```

### Methods

```cpp
void setNodeId(uint nodeId);
uint nodeId() const;

void setFd(uint fd);    // Ownership transferred
void resetFd();
uint fd() const;

QSize streamSize() const;
bool isReady() const;
bool usingDmaBuf() const;
bool allowDmaBuf() const;
void setAllowDmaBuf(bool allowed);

QString error() const;
StreamState state() const;
```

### Signals

```cpp
void nodeIdChanged(uint);
void fdChanged(uint);
void streamSizeChanged();
void stateChanged();
void usingDmaBufChanged();
void readyChanged();
```

---

## PipeWireSourceStream (Low-Level)

Manages the raw PipeWire stream and frame reception.

### UsageHint Enum

```cpp
enum class UsageHint {
    Render,           // Default: screen rendering
    EncodeSoftware,   // Software encoding
    EncodeHardware,   // Hardware encoding
};
```

### Key Methods

```cpp
bool createStream(uint nodeid, int fd);
void setActive(bool active);
void setMaxFramerate(const Fraction &framerate);
void setDamageEnabled(bool enabled);   // Track dirty regions
void setUsageHint(UsageHint hint);
QSize size() const;
Fraction framerate() const;
```

### Frame Data Structures

```cpp
struct PipeWireFrame {
    spa_video_format format;
    std::optional<quint64> sequential;            // Frame number
    std::optional<nanoseconds> presentationTimestamp;
    std::optional<DmaBufAttributes> dmabuf;       // GPU buffer
    std::optional<QRegion> damage;                 // Dirty regions
    std::optional<PipeWireCursor> cursor;          // Cursor info
    std::shared_ptr<PipeWireFrameData> dataFrame;  // CPU data
};

struct DmaBufAttributes {
    int width, height;
    uint32_t format;       // DRM pixel format
    uint64_t modifier;     // Layout modifier
    QList<DmaBufPlane> planes;
};

struct PipeWireFrameData {
    QImage toImage() const;                   // Convert to QImage
    std::shared_ptr<PipeWireFrameData> copy(); // Deep copy
};

struct PipeWireCursor {
    QPoint position;
    QPoint hotspot;
    QImage texture;
};
```

---

## Recording (PipeWireRecord)

For screen/window recording (not directly needed for dock thumbnails).

### Encoder Enum

```cpp
enum Encoder {
    NoEncoder, VP8, VP9, H264Main, H264Baseline, WebP, Gif
};
```

### State Enum

```cpp
enum State { Idle, Recording, Rendering };
```

### EncodingPreference

```cpp
enum EncodingPreference { NoPreference, Quality, Speed, Size };
```

---

## Usage Flow for Window Thumbnails

### Step 1: Get Window's PipeWire Stream

To get a PipeWire stream for a window, you need to use KWin's Screencast D-Bus interface:

```
org.kde.KWin → /Screencasting
Interface: org.kde.KWin.ScreencastV1

Method: StreamWindow(QString uuid, CursorMode cursorMode) → QDBusUnixFileDescriptor, uint nodeId
```

The `uuid` comes from the window's internal ID (available through KWin D-Bus or TaskManager).

### Step 2: Render in QML

```qml
import org.kde.pipewire as PipeWire

PipeWire.PipeWireSourceItem {
    id: thumbnailItem
    nodeId: windowNodeId   // From screencast request
    visible: ready

    width: parent.width
    height: width * (streamSize.height / Math.max(streamSize.width, 1))
}
```

### Step 3: Manage Lifecycle

```qml
// Start stream when tooltip/preview is shown
onVisibleChanged: {
    if (visible) {
        // Request screencast via D-Bus
        // Set nodeId on PipeWireSourceItem
    } else {
        thumbnailItem.nodeId = 0;  // Stop stream
    }
}
```

---

## Design Notes

1. **DMA-Buffer**: Hardware-accelerated GPU-to-GPU transfer; falls back to CPU if unavailable
2. **Async**: Frame reception is asynchronous; use `ready` property to show content
3. **Damage tracking**: Only dirty regions updated — efficient for live previews
4. **Resource management**: Set `nodeId = 0` or `resetFd()` to stop the stream and free resources
5. **Screencast permission**: KWin's screencast may require portal permission on Flatpak
6. **Frame timing**: `presentationTimestamp` allows frame-accurate presentation
