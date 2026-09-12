#pragma once

#include "../KremaGlobals.hpp"
#include "krema_core_export.h"
#include <QList>
#include <QObject>

namespace Krema
{

class BaseIsland;

/**
 * @brief BasePanel represents the global dock container (Tier 1).
 */
class KREMA_CORE_EXPORT BasePanel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(ScreenEdge edge READ edge WRITE setEdge NOTIFY edgeChanged)
    Q_PROPERTY(float width READ width WRITE setWidth NOTIFY widthChanged)
    Q_PROPERTY(float floatingOffset READ floatingOffset WRITE setFloatingOffset NOTIFY floatingOffsetChanged)
    Q_PROPERTY(float thickness READ thickness WRITE setThickness NOTIFY thicknessChanged)
    Q_PROPERTY(QList<BaseIsland *> islands READ islands NOTIFY islandsChanged)
    Q_PROPERTY(QVariantList islandsVariant READ islandsVariant NOTIFY islandsChanged)

public:
    enum class SpanMode {
        FitContent,
        FixedWidth,
        MinimumWidth,
        FillScreen
    };
    Q_ENUM(SpanMode)

    explicit BasePanel(QObject *parent = nullptr);
    ~BasePanel() override;

    ScreenEdge edge() const
    {
        return m_edge;
    }
    void setEdge(ScreenEdge edge);

    float width() const
    {
        return m_width;
    }
    void setWidth(float width);

    float floatingOffset() const
    {
        return m_floatingOffset;
    }
    void setFloatingOffset(float offset);

    float thickness() const
    {
        return m_thickness;
    }
    void setThickness(float thickness);

    QList<BaseIsland *> islands() const
    {
        return m_islands;
    }
    QVariantList islandsVariant() const;

    void addIsland(BaseIsland *island);
    void removeIsland(BaseIsland *island);

signals:
    void edgeChanged();
    void widthChanged();
    void floatingOffsetChanged();
    void thicknessChanged();
    void islandsChanged();

private:
    ScreenEdge m_edge = ScreenEdge::Bottom;
    float m_width = 0.0f;
    float m_floatingOffset = 8.0f;
    float m_thickness = 64.0f;
    QList<BaseIsland *> m_islands;
};

} // namespace Krema
