#pragma once

#include "../KremaGlobals.hpp"
#include "krema_core_export.h"
#include <cstddef>
#include <cstdint>

namespace Krema
{
namespace Debug
{

// Indicator Display Modes for Items (Atomic Capsules)
enum class IndicatorMode {
    IconAnchored,
    FullSpan,
    LeadingEdge,
    Hidden
};

enum class LayoutMode {
    IconOnly,
    IconLabel
};

enum class ItemType {
    App,
    Folder,
    Widget
};

// Rule 13: "pointer-based structs", "raw state of the Item Anatomy"
struct KREMA_CORE_EXPORT ItemTelemetry {
    uint32_t slotId;
    ItemType type;

    float itemContentSize;
    float itemIndicatorGap;
    float itemIndicatorSize;
    IndicatorMode indicatorMode;

    bool zoomEnabled;

    float targetContentSize;
    float actualContentSize;
    float scaleFactor;
    float currentX;
    float currentY;

    bool isInHoverBoundary;
    bool isInKineticOrbit;

    // Instance & Focus Tracking
    uint32_t instanceCount; // Number of open windows for this item
    int32_t activeInstanceIndex; // Index of the focused window (-1 if none)

    // Attention & Notifications (M2.1)
    Krema::UrgencyLevel urgency;
    uint32_t notificationCount;

    // Recursive Folder Support
    bool isExpanded;
    ItemTelemetry *subItems;
    size_t subItemCount;
};

struct KREMA_CORE_EXPORT IslandTelemetry {
    uint32_t islandId;
    float islandPadding;

    bool zoomEnabled; // Island-level lock: overrides child items if false

    ItemTelemetry *items; // Pointer to array of Items in this Island
    size_t itemCount;
};

// Overflow handling for fixed/minimum width panels
enum class OverflowMode {
    Squish, // Dynamically scale down items to fit
    Scroll // Maintain size, enable horizontal scrolling
};

enum class ScreenEdge {
    Bottom,
    Top,
    Left,
    Right
};

enum class PanelAlignment {
    Start,
    Center,
    End
};

enum class ContentAlignment {
    Start,
    Center,
    End,
    Justify
};

struct KREMA_CORE_EXPORT LayoutTelemetry {
    LayoutMode layoutMode;
    ScreenEdge edge; // The anchored screen edge
    PanelAlignment panelAlignment;
    ContentAlignment contentAlignment;

    float rawMouseX;
    float rawMouseY;

    IslandTelemetry *islands; // Pointer to array of Islands in this Panel
    size_t islandCount;

    float calculatedIslandGap;

    // Fixed Width / Overflow variables
    OverflowMode overflowMode;
    float dynamicScaleMultiplier; // E.g., 0.85 if squished
    float canvasOffsetX; // Translation for scrolling
    bool canScrollLeft;
    bool canScrollRight;
};

// Publisher interface to be implemented by engine
class KREMA_CORE_EXPORT TelemetryPublisher
{
public:
    virtual ~TelemetryPublisher() = default;

    virtual void publish(const LayoutTelemetry *data) = 0;
    virtual bool isListenerActive() const = 0;
};

} // namespace Debug
} // namespace Krema
