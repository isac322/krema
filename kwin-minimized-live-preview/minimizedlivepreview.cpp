#include <kwin/effect/effect.h>
#include <kwin/effect/effecthandler.h>
#include <kwin/effect/effectwindow.h>
#include <kwin/window.h>

#include <QHash>
#include <QPointer>

namespace KWin
{

class MinimizedLivePreviewEffect final : public Effect
{
    Q_OBJECT

public:
    MinimizedLivePreviewEffect()
    {
        connect(effects, &EffectsHandler::windowAdded, this, &MinimizedLivePreviewEffect::observeWindow);
        connect(effects, &EffectsHandler::windowClosed, this, &MinimizedLivePreviewEffect::releaseWindow);
        connect(effects, &EffectsHandler::windowDeleted, this, &MinimizedLivePreviewEffect::releaseWindow);

        for (EffectWindow *window : effects->stackingOrder()) {
            observeWindow(window);
        }
    }

    ~MinimizedLivePreviewEffect() override
    {
        const auto windows = m_windows.keys();
        for (EffectWindow *window : windows) {
            releaseWindow(window);
        }
    }

private Q_SLOTS:
    void observeWindow(EffectWindow *effectWindow)
    {
        if (!effectWindow) {
            return;
        }

        connect(effectWindow, &EffectWindow::minimizedChanged,
                this, &MinimizedLivePreviewEffect::syncWindow, Qt::UniqueConnection);
        syncWindow(effectWindow);
    }

    void syncWindow(EffectWindow *effectWindow)
    {
        if (!effectWindow) {
            return;
        }

        const auto existing = m_windows.constFind(effectWindow);
        if (effectWindow->isMinimized()) {
            if (existing == m_windows.cend()) {
                if (Window *window = effectWindow->window()) {
                    window->refOffscreenRendering();
                    m_windows.insert(effectWindow, window);
                }
            }
        } else if (existing != m_windows.cend()) {
            releaseWindow(effectWindow);
        }
    }

    void releaseWindow(EffectWindow *effectWindow)
    {
        const auto existing = m_windows.find(effectWindow);
        if (existing == m_windows.end()) {
            return;
        }

        if (existing.value()) {
            existing.value()->unrefOffscreenRendering();
        }
        m_windows.erase(existing);
    }

private:
    QHash<EffectWindow *, QPointer<Window>> m_windows;
};

} // namespace KWin

KWIN_EFFECT_FACTORY(KWin::MinimizedLivePreviewEffect, "metadata.json")

#include "minimizedlivepreview.moc"
