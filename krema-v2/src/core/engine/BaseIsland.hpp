#pragma once

#include "krema_core_export.h"
#include <QList>
#include <QObject>

namespace Krema
{

class BaseItem;

/**
 * @brief BaseIsland represents a logical group of items (Tier 2).
 * Manages its own padding and zoom lock.
 */
class KREMA_CORE_EXPORT BaseIsland : public QObject
{
    Q_OBJECT
    Q_PROPERTY(uint32_t islandId READ islandId CONSTANT)
    Q_PROPERTY(float x READ x WRITE setX NOTIFY xChanged)
    Q_PROPERTY(float width READ width WRITE setWidth NOTIFY widthChanged)
    Q_PROPERTY(float padding READ padding WRITE setPadding NOTIFY paddingChanged)
    Q_PROPERTY(float spacing READ spacing WRITE setSpacing NOTIFY spacingChanged)
    Q_PROPERTY(bool zoomEnabled READ isZoomEnabled WRITE setZoomEnabled NOTIFY zoomEnabledChanged)
    Q_PROPERTY(QVariantList items READ itemsVariant NOTIFY itemsChanged)

public:
    explicit BaseIsland(uint32_t islandId, QObject *parent = nullptr);
    ~BaseIsland() override;

    uint32_t islandId() const
    {
        return m_islandId;
    }

    float x() const
    {
        return m_x;
    }
    void setX(float x);

    float width() const
    {
        return m_width;
    }
    void setWidth(float width);

    float padding() const
    {
        return m_padding;
    }
    void setPadding(float padding);

    float spacing() const
    {
        return m_spacing;
    }
    void setSpacing(float spacing);

    bool isZoomEnabled() const
    {
        return m_zoomEnabled;
    }
    void setZoomEnabled(bool enabled);

    QList<BaseItem *> items() const
    {
        return m_items;
    }
    QVariantList itemsVariant() const;

    /**
     * @brief Add an item to this island.
     */
    void addItem(BaseItem *item);

    /**
     * @brief Remove an item from this island.
     */
    void removeItem(BaseItem *item);

signals:
    void xChanged();
    void widthChanged();
    void paddingChanged();
    void spacingChanged();
    void zoomEnabledChanged();
    void itemsChanged();

private:
    uint32_t m_islandId;
    float m_x = 0.0f;
    float m_width = 0.0f;
    float m_padding = 8.0f;
    float m_spacing = 4.0f;
    bool m_zoomEnabled = true;
    QList<BaseItem *> m_items;
};

} // namespace Krema
