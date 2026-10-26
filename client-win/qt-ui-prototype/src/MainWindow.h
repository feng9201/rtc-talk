#pragma once

#include "app/UiController.h"
#include "base/FramelessWindow.h"

class PrejoinPage;
class QStackedWidget;
class RoomPage;
class WindowControls;

class MainWindow final : public FramelessWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    void startScenario(Scenario scenario);
    void setStageLayout(StageLayout layout);
    void setStageFilter(StageFilter filter);
    void performAction(const QString &action);
    void setPanelTab(PanelTab tab);
    void selectMember(const QString &id);
    void showSettingsDialog();
    void showLeaveDialog();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildUi();
    void wirePages();
    void showPrejoinPage();
    void showRoomPage();
    void positionWindowControls();

    UiController m_controller;
    QStackedWidget *m_stack = nullptr;
    PrejoinPage *m_prejoinPage = nullptr;
    RoomPage *m_roomPage = nullptr;
    WindowControls *m_windowControls = nullptr;
};
