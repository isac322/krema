#pragma once

#include <QObject>
#include <QRegion>
#include <QSize>
#include <QString>

namespace Krema
{

/**
 * @brief IProtocol is the abstraction layer for Wayland shell protocols (KWin/Layer-Shell vs Hyprland).
 * It handles surface lifecycle, anchors, and interaction regions.
 */
class IProtocol : public QObject
{
    Q_OBJECT
public:
    enum class Layer {
        Background,
        Bottom,
        Top,
        Overlay
    };

    explicit IProtocol(QObject *parent = nullptr)
        : QObject(parent)
    {
    }
    virtual ~IProtocol() = default;

    /**
     * @brief Initialize the shell connection.
     * @return true if successful.
     */
    virtual bool initialize() = 0;

    /**
     * @brief Associate a window with this protocol instance.
     */
    virtual void setWindow(class QWindow *window) = 0;

    /**
     * @brief Set the visual layer of the surface.
     */
    Q_INVOKABLE virtual void setLayer(Layer layer) = 0;

    /**
     * @brief Define the exclusive zone (distance from edge reserved for the dock).
     */
    Q_INVOKABLE virtual void setExclusiveZone(int zone) = 0;

    /**
     * @brief Set the input region (hitbox) for the surface.
     */
    Q_INVOKABLE virtual void setInputRegion(const QRect &rect) = 0;

    /**
     * @brief Define the blurred region of the surface.
     */
    Q_INVOKABLE virtual void setBlurRegion(const QRect &rect) = 0;

    /**
     * @brief Set the surface margins relative to the anchors.
     */
    Q_INVOKABLE virtual void setMargins(int top, int right, int bottom, int left) = 0;

    /**
     * @brief Set the edge anchors (e.g., Qt::BottomEdge).
     */
    Q_INVOKABLE virtual void setAnchors(Qt::Edges anchors) = 0;

    /**
     * @brief Request a specific size from the compositor.
     */
    Q_INVOKABLE virtual void requestSize(const QSize &size) = 0;

    /**
     * @brief Get the name of the active shell.
     */
    virtual QString shellName() const = 0;

signals:
    /**
     * @brief Emitted when the shell is fully initialized.
     */
    void initialized();

    /**
     * @brief Emitted when the surface geometry is configured by the compositor.
     */
    void surfaceConfigured(const QSize &size);

    /**
     * @brief Emitted when the output/monitor state changes.
     */
    void outputChanged();
};

} // namespace Krema
