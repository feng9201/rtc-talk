#include "MainWindow.h"

#include "pages/PrejoinPage.h"
#include "pages/RoomPage.h"
#include "widgets/Toast.h"
#include "widgets/WindowControls.h"

#include <QResizeEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

MainWindow::MainWindow(QWidget *parent)
    : FramelessWindow(parent)
{
    setObjectName(QStringLiteral("MainWindow"));
    setWindowTitle(QStringLiteral("RTC Talk"));
    buildUi();
    wirePages();
    showPrejoinPage();
}

void MainWindow::startScenario(Scenario scenario)
{
    if (scenario == Scenario::Prejoin) {
        showPrejoinPage();
        return;
    }
    m_controller.startScenario(scenario);
}

void MainWindow::setStageLayout(StageLayout layout)
{
    if (m_controller.state().joined) m_controller.setStageLayout(layout);
}

void MainWindow::setStageFilter(StageFilter filter)
{
    if (m_controller.state().joined) m_controller.setStageFilter(filter);
}
void MainWindow::performAction(const QString &action)
{
    if (!m_controller.state().joined) return;
    if (action == QStringLiteral("mic")) m_controller.toggleMic();
    else if (action == QStringLiteral("camera")) m_controller.toggleCamera();
    else if (action == QStringLiteral("share")) m_controller.toggleShare();
    else if (action == QStringLiteral("deafen")) m_controller.toggleDeafen();
    else if (action == QStringLiteral("muteall")) m_controller.muteAll();
    else if (action == QStringLiteral("source-system")) m_controller.setSource(QStringLiteral("system"));
    else if (action == QStringLiteral("source-mic")) m_controller.setSource(QStringLiteral("mic"));
}
void MainWindow::setPanelTab(PanelTab tab)
{
    if (m_controller.state().joined) m_controller.setPanelTab(tab);
}
void MainWindow::selectMember(const QString &id)
{
    if (m_controller.state().joined) m_controller.selectMember(id);
}
void MainWindow::showSettingsDialog()
{
    if (m_controller.state().joined) m_roomPage->showSettingsDialog();
}

void MainWindow::showLeaveDialog()
{
    if (m_controller.state().joined) m_roomPage->showLeaveConfirmation();
}
void MainWindow::buildUi()
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("PageStack"));
    m_prejoinPage = new PrejoinPage(m_stack);
    m_roomPage = new RoomPage(m_stack);
    m_stack->addWidget(m_prejoinPage);
    m_stack->addWidget(m_roomPage);
    layout->addWidget(m_stack);

    m_windowControls = new WindowControls(this);
    positionWindowControls();
    m_windowControls->raise();
}

void MainWindow::wirePages()
{
    connect(m_prejoinPage, &PrejoinPage::joinRequested,
            &m_controller, &UiController::join);
    connect(m_prejoinPage, &PrejoinPage::cameraPreviewToggled,
            &m_controller, &UiController::setPreview);
    connect(m_prejoinPage, &PrejoinPage::saveSettingsRequested, this, [this](const JoinOptions &) {
        Toast::showMessage(this, QStringLiteral("入房参数已保存（mock）"), QStringLiteral("ok"));
    });

    connect(&m_controller, &UiController::joined, this, [this] {
        showRoomPage();
        m_roomPage->setState(m_controller.state());
    });
    connect(&m_controller, &UiController::left, this, &MainWindow::showPrejoinPage);
    connect(&m_controller, &UiController::stateChanged, this, [this] {
        m_prejoinPage->setPreviewEnabled(m_controller.state().preview);
        if (m_controller.state().joined) {
            m_roomPage->setState(m_controller.state());
            m_roomPage->setStatsJson(m_controller.statsJson());
        }
    });
    connect(&m_controller, &UiController::elapsedChanged,
            m_roomPage, &RoomPage::setElapsed);
    connect(&m_controller, &UiController::toastRequested, this,
            [this](const QString &text, const QString &tone) {
                Toast::showMessage(this, text, tone);
            });
    connect(&m_controller, &UiController::diagnosticsChanged, this, [this](bool open) {
        m_roomPage->setDiagnosticsOpen(open);
        m_windowControls->setVisible(!open);
        if (!open) m_windowControls->raise();
    });
    connect(m_roomPage, &RoomPage::micToggleRequested,
            &m_controller, &UiController::toggleMic);
    connect(m_roomPage, &RoomPage::cameraToggleRequested,
            &m_controller, &UiController::toggleCamera);
    connect(m_roomPage, &RoomPage::shareToggleRequested,
            &m_controller, &UiController::toggleShare);
    connect(m_roomPage, &RoomPage::deafenToggleRequested,
            &m_controller, &UiController::toggleDeafen);
    connect(m_roomPage, &RoomPage::leaveRequested,
            &m_controller, &UiController::leaveRoom);
    connect(m_roomPage, &RoomPage::panelTabRequested,
            &m_controller, &UiController::setPanelTab);
    connect(m_roomPage, &RoomPage::stageFilterRequested,
            &m_controller, &UiController::setStageFilter);
    connect(m_roomPage, &RoomPage::stageLayoutRequested,
            &m_controller, &UiController::setStageLayout);
    connect(m_roomPage, &RoomPage::memberSelected,
            &m_controller, &UiController::selectMember);
    connect(m_roomPage, &RoomPage::memberFocusRequested, this, [this](const QString &id) {
        m_controller.selectMember(id);
        m_controller.setStageLayout(StageLayout::Focus);
    });
    connect(m_roomPage, &RoomPage::memberActionRequested,
            &m_controller, &UiController::memberAction);
    connect(m_roomPage, &RoomPage::sourceRequested,
            &m_controller, &UiController::setSource);
    connect(m_roomPage, &RoomPage::volumeChanged,
            &m_controller, &UiController::setVolume);
    connect(m_roomPage, &RoomPage::deviceChanged, this,
            [this](const QString &kind, const QString &id) {
                if (kind == QStringLiteral("microphone") || kind == QStringLiteral("mic")) {
                    m_controller.setMicDevice(id);
                } else if (kind == QStringLiteral("playout")) {
                    m_controller.setPlayoutDevice(id);
                } else if (kind == QStringLiteral("camera")) {
                    m_controller.setCameraDevice(id);
                }
            });
    connect(m_roomPage, &RoomPage::previewChanged,
            &m_controller, &UiController::setPreview);
    connect(m_roomPage, &RoomPage::muteAllRequested,
            &m_controller, &UiController::muteAll);
    connect(m_roomPage, &RoomPage::refreshDevicesRequested,
            &m_controller, &UiController::refreshDevices);
    connect(m_roomPage, &RoomPage::diagnosticsRequested,
            &m_controller, &UiController::openDiagnostics);
    connect(m_roomPage, &RoomPage::diagnosticsClosed,
            &m_controller, &UiController::closeDiagnostics);
}

void MainWindow::showPrejoinPage()
{
    m_stack->setCurrentWidget(m_prejoinPage);
    m_prejoinPage->setPreviewEnabled(m_controller.state().preview);
    m_windowControls->raise();
}

void MainWindow::showRoomPage()
{
    m_stack->setCurrentWidget(m_roomPage);
    m_windowControls->raise();
}

void MainWindow::positionWindowControls()
{
    if (!m_windowControls) return;
    m_windowControls->move(width() - m_windowControls->width() - 18, 18);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    FramelessWindow::resizeEvent(event);
    positionWindowControls();
}
