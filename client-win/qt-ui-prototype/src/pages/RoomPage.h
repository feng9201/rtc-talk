#pragma once

#include "app/UiState.h"
#include "base/Theme.h"

#include <QPair>
#include <QWidget>

class ControlButton;
class QCheckBox;
class QComboBox;
class Pill;
class QDialog;
class QLabel;
class QScrollArea;
class QSlider;
class QTextEdit;
class QVBoxLayout;
class SegmentedControl;
class VideoGrid;

class RoomPage : public QWidget
{
    Q_OBJECT

public:
    explicit RoomPage(QWidget *parent = nullptr);

    void setState(const UiState &state);
    void setElapsed(int seconds);
    void setStatsJson(const QString &json);
    void setDiagnosticsOpen(bool open);
    void showLeaveConfirmation();
    void showSettingsDialog();

signals:
    void micToggleRequested();
    void cameraToggleRequested();
    void shareToggleRequested();
    void deafenToggleRequested();
    void settingsRequested();
    void leaveRequested();
    void panelTabRequested(PanelTab tab);
    void stageFilterRequested(StageFilter filter);
    void stageLayoutRequested(StageLayout layout);
    void memberSelected(const QString &id);
    void memberFocusRequested(const QString &id);
    void memberActionRequested(const QString &id, const QString &action);
    void sourceRequested(const QString &source);
    void volumeChanged(int volume);
    void deviceChanged(const QString &kind, const QString &id);
    void previewChanged(bool enabled);
    void muteAllRequested();
    void refreshDevicesRequested();
    void diagnosticsRequested();
    void diagnosticsClosed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QWidget *buildHeader();
    QWidget *buildStage();
    QWidget *buildControlBar();
    QWidget *buildSidebar();
    QWidget *buildDiagnostics();
    QWidget *buildMemberRow(const MemberInfo &member);
    QWidget *buildDevicesPanel();
    QWidget *buildStatusPanel();
    QWidget *buildSelfFooter();
    QWidget *buildDeviceCard(const QString &sourceId, const QString &title,
                             const QString &description, Theme::Icon icon);
    QDialog *buildSettingsDialog();
    QDialog *buildLeaveDialog();
    QWidget *makeStatusRow(const QString &label, const QString &value, const QString &tone = QString());
    QComboBox *makeDeviceCombo(const QString &kind, const QString &current,
                               const QVector<QPair<QString, QString>> &items, QWidget *parent);

    void rebuildSidePanel();
    void updateHeader();
    void updateControls();
    void updateDiagnostics();
    void syncSettingsControls();
    void positionOverlays();
    void centerDialog(QDialog *dialog);

    UiState m_state;
    QString m_statsJson;
    bool m_diagnosticsOpen = false;

    QLabel *m_roomTitle = nullptr;
    QLabel *m_roomId = nullptr;
    Pill *m_connectionPill = nullptr;
    Pill *m_hostPill = nullptr;
    Pill *m_sharePill = nullptr;
    Pill *m_modePill = nullptr;
    Pill *m_directPill = nullptr;
    Pill *m_memberCountPill = nullptr;
    Pill *m_videoCountPill = nullptr;
    Pill *m_clockPill = nullptr;
    Pill *m_prototypePill = nullptr;
    Pill *m_floorPill = nullptr;

    SegmentedControl *m_filterControl = nullptr;
    SegmentedControl *m_layoutControl = nullptr;
    SegmentedControl *m_tabControl = nullptr;
    VideoGrid *m_videoGrid = nullptr;

    ControlButton *m_micControl = nullptr;
    ControlButton *m_cameraControl = nullptr;
    ControlButton *m_shareControl = nullptr;
    ControlButton *m_listenControl = nullptr;
    ControlButton *m_settingsControl = nullptr;
    ControlButton *m_leaveControl = nullptr;
    QLabel *m_controlHint = nullptr;

    QScrollArea *m_sideScroll = nullptr;
    QWidget *m_sideBody = nullptr;
    QVBoxLayout *m_sideBodyLayout = nullptr;
    QWidget *m_sideFoot = nullptr;
    QVBoxLayout *m_sideFootLayout = nullptr;

    QWidget *m_diagnosticsOverlay = nullptr;
    QWidget *m_diagnosticsDrawer = nullptr;
    QTextEdit *m_diagnosticsLog = nullptr;
    QTextEdit *m_diagnosticsStats = nullptr;

    QDialog *m_settingsDialog = nullptr;
    SegmentedControl *m_settingsSource = nullptr;
    QComboBox *m_settingsMic = nullptr;
    QComboBox *m_settingsPlayout = nullptr;
    QComboBox *m_settingsCamera = nullptr;
    QSlider *m_settingsVolume = nullptr;
    QLabel *m_settingsVolumeValue = nullptr;
    QComboBox *m_panelMic = nullptr;
    QComboBox *m_panelPlayout = nullptr;
    QComboBox *m_panelCamera = nullptr;
    QSlider *m_panelVolume = nullptr;
    QLabel *m_panelVolumeValue = nullptr;
    QCheckBox *m_panelPreview = nullptr;
    QLabel *m_statusElapsed = nullptr;

    QDialog *m_leaveDialog = nullptr;
};
