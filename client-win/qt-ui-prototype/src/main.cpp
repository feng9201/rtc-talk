#include "base/FramelessWindow.h"
#include "base/Theme.h"
#include "pages/PrejoinPage.h"

#include <QApplication>
#include <QVBoxLayout>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("RTC Talk UI Prototype"));
    app.setOrganizationName(QStringLiteral("RTC Talk"));

    Theme::apply(app);

    FramelessWindow window;
    window.setObjectName(QStringLiteral("MainWindow"));
    window.resize(1440, 900);

    auto *layout = new QVBoxLayout(&window);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(new PrejoinPage(&window));

    window.show();
    return app.exec();
}
