#pragma once

#include "app/UiState.h"

#include <QObject>
#include <QTimer>

class UiController final : public QObject
{
    Q_OBJECT

public:
    explicit UiController(QObject *parent = nullptr);

    const UiState &state() const { return m_state; }
    UiState &mutableState() { return m_state; }
    const MemberInfo *memberById(const QString &id) const;
    const MemberInfo *selectedMember() const;
    QString statsJson() const;

public slots:
    void join(const JoinOptions &options);
    void leaveRoom();
    void startScenario(Scenario scenario);
    void toggleMic();
    void toggleCamera();
    void toggleShare();
    void toggleDeafen();
    void setSource(const QString &source);
    void selectMember(const QString &id);
    void setPanelTab(PanelTab tab);
    void setStageFilter(StageFilter filter);
    void setStageLayout(StageLayout layout);
    void muteAll();
    void memberAction(const QString &id, const QString &action);
    void setMicDevice(const QString &id);
    void setPlayoutDevice(const QString &id);
    void setCameraDevice(const QString &id);
    void setVolume(int volume);
    void setPreview(bool enabled);
    void refreshDevices();
    void openDiagnostics();
    void closeDiagnostics();
    void setScenario(Scenario scenario);

signals:
    void joined();
    void left();
    void stateChanged();
    void toastRequested(const QString &text, const QString &tone);
    void elapsedChanged(int seconds);
    void diagnosticsChanged(bool open);

private:
    void seedMembers(Scenario scenario);
    void syncSelfMember();
    void addLog(const QString &text);
    void emitChanged();
    QString effectiveMediaMode() const;

    UiState m_state;
    QTimer m_clock;
};
