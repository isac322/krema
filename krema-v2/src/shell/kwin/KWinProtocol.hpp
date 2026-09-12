#pragma once

#include "../IProtocol.hpp"
#include "krema_shell_kwin_export.h"

class QWindow;

namespace LayerShellQt
{
class Window;
}

namespace Krema
{

/**
 * @brief KWin implementation of IProtocol using LayerShellQt.
 */
class KREMA_SHELL_KWIN_EXPORT KWinProtocol : public IProtocol
{
    Q_OBJECT
public:
    explicit KWinProtocol(QObject *parent = nullptr);
    ~KWinProtocol() override;

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
        return QStringLiteral("KWin (Layer-Shell)");
    }

private:
    LayerShellQt::Window *m_layerWindow = nullptr;
    QWindow *m_window = nullptr;
};

} // namespace Krema
