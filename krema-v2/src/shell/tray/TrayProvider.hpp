#pragma once

#include <QList>
#include <QObject>
#include <QVariant>

namespace Krema
{

struct TrayEntry {
    QString id;
    QString title;
    QString icon;
};

/**
 * @brief Provider for System Tray (SNI) icons.
 */
class TrayProvider : public QObject
{
    Q_OBJECT
public:
    explicit TrayProvider(QObject *parent = nullptr);
    ~TrayProvider() override;

    QList<TrayEntry> items() const;

signals:
    void itemsChanged();

private:
    QList<TrayEntry> m_items;
};

} // namespace Krema
