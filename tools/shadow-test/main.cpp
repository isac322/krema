#include <QGuiApplication>
#include <QQuickStyle>
#include <QQuickView>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQuickView view;
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    view.setSource(QUrl(QStringLiteral("qrc:/ShadowTest/main.qml")));
    view.resize(900, 700);
    view.show();

    return app.exec();
}
