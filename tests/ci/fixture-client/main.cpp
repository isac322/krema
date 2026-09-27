// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Krema Contributors
//
// Minimal xdg_toplevel Wayland client used as a window fixture for frame
// scenarios. krema's TasksModel only grows window rows when something actually
// maps a toplevel surface on the compositor, so scenarios that exercise
// previews, running indicators or active state need one of these running.
//
// Usage: krema-fixture-window <title> <desktop-file-name>

#include <QGuiApplication>
#include <QPainter>
#include <QRect>
#include <QRasterWindow>

namespace
{

class FixtureWindow : public QRasterWindow
{
public:
    explicit FixtureWindow(const QString &label)
        : m_label(label)
    {
        setTitle(label);
        resize(320, 240);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.fillRect(0, 0, width(), height(), QColor(0x30, 0x40, 0x60));
        painter.setPen(Qt::white);
        painter.drawText(QRect(0, 0, width(), height()), Qt::AlignCenter, m_label);
    }

private:
    QString m_label;
};

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const QString label = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("fixture");
    // app_id drives how TasksModel groups and labels the row.
    QGuiApplication::setDesktopFileName(argc > 2 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("org.kde.kcalc"));
    FixtureWindow window(label);
    window.show();
    return app.exec();
}
