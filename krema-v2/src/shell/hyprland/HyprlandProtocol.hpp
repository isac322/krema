#pragma once

#include "../IProtocol.hpp"
#include "krema_shell_hyprland_export.h"

class QWindow;

namespace LayerShellQt
{
class Window;
}

namespace Krema
{

/**
 * @brief Hyprland implementation of IProtocol using LayerShellQt.
 * Hyprland supports wlr-layer-shell, which LayerShellQt wraps.
 */
class KREMA_SHELL_HYPRLAND_EXPORT HyprlandProtocol : public IProtocol
{
    Q_OBJECT
public:
    explicit HyprlandProtocol(QObject *parent = nullptr);
    ~HyprlandProtocol() override;

    bool initialize() override;
    void setWindow(QWindow *window) override;
    void setLayer(Layer layer) override;
    void setExclusiveZone(int zone) override;
    void setInputRegion(const QRect &rect) override;
    void setBlurRegion(const QRect &rect) override;
    void setMargins(int top, int right, int bottom, int left) override;
    void setAnchors(Qt::Edges anchors) override;
    void requestSize(const QSize &size) override;
    QString shellName() const override
    {
        return QStringLiteral("Hyprland (Layer-Shell)");
    }

private:
    LayerShellQt::Window *m_layerWindow = nullptr;
    QWindow *m_window = nullptr;
};

} // namespace Krema
