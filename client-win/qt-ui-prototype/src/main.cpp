#include "MainWindow.h"
#include "base/Theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QScreen>
#include <QTimer>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("RTC Talk UI Prototype"));
    app.setOrganizationName(QStringLiteral("RTC Talk"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("RTC Talk Qt Widgets UI prototype"));
    parser.addHelpOption();
    const QCommandLineOption scenarioOption(QStringLiteral("scenario"),
        QStringLiteral("Start scenario: prejoin|multi|one|sharing|devices|diagnostics"),
        QStringLiteral("name"), QStringLiteral("prejoin"));
    const QCommandLineOption layoutOption(QStringLiteral("layout"),
        QStringLiteral("Stage layout: grid|focus"), QStringLiteral("name"));
    const QCommandLineOption selectedOption(QStringLiteral("selected"),
        QStringLiteral("Selected member id"), QStringLiteral("id"));
    const QCommandLineOption panelOption(QStringLiteral("panel"),
        QStringLiteral("Panel tab: members|devices|status"), QStringLiteral("name"));
    const QCommandLineOption filterOption(QStringLiteral("filter"),
        QStringLiteral("Stage filter: all|video"), QStringLiteral("name"));
    const QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
        QStringLiteral("Save a window screenshot and exit"), QStringLiteral("path"));
    const QCommandLineOption actionOption(QStringLiteral("action"),
        QStringLiteral("Perform a mock action: mic|camera|share|deafen"), QStringLiteral("name"));
    const QCommandLineOption sizeOption(QStringLiteral("size"),
        QStringLiteral("Window size in WxH form"), QStringLiteral("size"), QStringLiteral("1440x900"));
    const QCommandLineOption showSettingsOption(QStringLiteral("show-settings"),
        QStringLiteral("Open the mock settings dialog for visual verification"));
    const QCommandLineOption showLeaveOption(QStringLiteral("show-leave"),
        QStringLiteral("Open the leave confirmation dialog for visual verification"));
    const QCommandLineOption smokeOption(QStringLiteral("smoke-seconds"),
        QStringLiteral("Exit after the specified number of seconds"), QStringLiteral("seconds"));
    parser.addOption(scenarioOption);
    parser.addOption(layoutOption);
    parser.addOption(selectedOption);
    parser.addOption(panelOption);
    parser.addOption(filterOption);
    parser.addOption(screenshotOption);
    parser.addOption(actionOption);
    parser.addOption(sizeOption);
    parser.addOption(showSettingsOption);
    parser.addOption(showLeaveOption);
    parser.addOption(smokeOption);
    parser.process(app);

    Theme::apply(app);

    QSize initialSize(1440, 900);
    const QStringList sizeParts = parser.value(sizeOption).split(QStringLiteral("x"), Qt::SkipEmptyParts);
    if (sizeParts.size() == 2) {
        const int width = sizeParts.at(0).toInt();
        const int height = sizeParts.at(1).toInt();
        if (width > 0 && height > 0) initialSize = QSize(width, height);
    }

    MainWindow window;
    window.resize(initialSize);
    window.show();

    const QString scenarioValue = parser.value(scenarioOption).trimmed().toLower();
    const QString layoutValue = parser.value(layoutOption).trimmed().toLower();
    const QString selectedValue = parser.value(selectedOption).trimmed();
    const QString panelValue = parser.value(panelOption).trimmed().toLower();
    const QString filterValue = parser.value(filterOption).trimmed().toLower();
    const QStringList actionValues = parser.values(actionOption);
    const bool showSettings = parser.isSet(showSettingsOption);
    const bool showLeave = parser.isSet(showLeaveOption);
    QTimer::singleShot(0, &window, [&window, scenarioValue, selectedValue, panelValue, layoutValue, filterValue, actionValues] {
        if (scenarioValue != QStringLiteral("prejoin"))
            window.startScenario(scenarioFromString(scenarioValue));
        if (!selectedValue.isEmpty())
            window.selectMember(selectedValue);
        if (panelValue == QStringLiteral("members"))
            window.setPanelTab(PanelTab::Members);
        else if (panelValue == QStringLiteral("devices"))
            window.setPanelTab(PanelTab::Devices);
        else if (panelValue == QStringLiteral("status"))
            window.setPanelTab(PanelTab::Status);
        if (layoutValue == QStringLiteral("focus"))
            window.setStageLayout(StageLayout::Focus);
        else if (layoutValue == QStringLiteral("grid"))
            window.setStageLayout(StageLayout::Grid);
        if (filterValue == QStringLiteral("video"))
            window.setStageFilter(StageFilter::VideoOnly);
        else if (filterValue == QStringLiteral("all"))
            window.setStageFilter(StageFilter::All);
        for (const QString &action : actionValues)
            window.performAction(action.trimmed().toLower());
    });

    if (showSettings || showLeave) {
        QTimer::singleShot(120, &window, [&window, showSettings, showLeave] {
            if (showSettings) window.showSettingsDialog();
            else if (showLeave) window.showLeaveDialog();
        });
    }
    const QString screenshotPath = parser.value(screenshotOption).trimmed();
    if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(900, &window, [&window, &app, screenshotPath] {
            const QString absolutePath = QFileInfo(screenshotPath).absoluteFilePath();
            QDir().mkpath(QFileInfo(absolutePath).absolutePath());
            QPixmap capture;
            if (QWidget *modal = QApplication::activeModalWidget()) {
                const QRect area = window.frameGeometry().united(modal->frameGeometry());
                if (QScreen *currentScreen = window.screen())
                    capture = currentScreen->grabWindow(0, area.x(), area.y(), area.width(), area.height());
            }
            if (capture.isNull()) capture = window.grab();
            const bool saved = capture.save(absolutePath);
            app.exit(saved ? 0 : 2);
        });
    } else {
        bool ok = false;
        const int seconds = parser.value(smokeOption).toInt(&ok);
        if (ok && seconds > 0) QTimer::singleShot(seconds * 1000, &app, &QCoreApplication::quit);
    }

    return app.exec();
}
