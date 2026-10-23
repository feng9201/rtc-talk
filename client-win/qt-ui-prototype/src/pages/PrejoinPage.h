#pragma once

#include "app/UiState.h"
#include "base/Theme.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class VideoTile;

class PrejoinPage : public QWidget
{
    Q_OBJECT

public:
    explicit PrejoinPage(QWidget *parent = nullptr);

    JoinOptions options() const;
    void setScenario(Scenario scenario);
    void setPreviewEnabled(bool enabled);

signals:
    void joinRequested(const JoinOptions &options);
    void saveSettingsRequested(const JoinOptions &options);
    void cameraPreviewToggled(bool enabled);

private:
    QWidget *buildHeader();
    QWidget *buildFormCard();
    QWidget *buildPreviewColumn();
    QWidget *buildSummaryCard();
    QWidget *buildSourceOption(const QString &id, const QString &title, const QString &description, Theme::Icon icon);
    QWidget *makeField(const QString &label, QWidget *editor, const QString &hint = QString());
    QWidget *makePair(QWidget *left, QWidget *right);
    void refreshSummary();
    void applySource(const QString &source);
    Scenario selectedScenario() const;
    QString comboData(const QComboBox *combo, const QString &fallback = QString()) const;

    QLineEdit *m_server = nullptr;
    QLineEdit *m_userId = nullptr;
    QLineEdit *m_roomId = nullptr;
    QLineEdit *m_token = nullptr;
    QCheckBox *m_devToken = nullptr;
    QComboBox *m_audioBackend = nullptr;
    QComboBox *m_mode = nullptr;
    QComboBox *m_talkMode = nullptr;
    QComboBox *m_scenario = nullptr;
    QComboBox *m_micDevice = nullptr;
    QComboBox *m_playoutDevice = nullptr;
    QComboBox *m_cameraDevice = nullptr;
    QSlider *m_volume = nullptr;
    QLabel *m_volumeValue = nullptr;
    QPushButton *m_micSource = nullptr;
    QPushButton *m_systemSource = nullptr;
    QPushButton *m_previewButton = nullptr;
    VideoTile *m_previewTile = nullptr;
    QLabel *m_summaryAudio = nullptr;
    QLabel *m_summaryVideo = nullptr;
    QLabel *m_summaryMode = nullptr;
    QLabel *m_summaryTalk = nullptr;
    QLabel *m_summaryScenario = nullptr;
    QString m_source = QStringLiteral("mic");
    bool m_previewEnabled = false;
};