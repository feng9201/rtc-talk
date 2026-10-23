#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

enum class Scenario {
    Prejoin,
    Multi,
    One,
    Sharing,
    Devices,
    Diagnostics
};

enum class PanelTab {
    Members,
    Devices,
    Status
};

enum class StageFilter {
    All,
    VideoOnly
};

enum class StageLayout {
    Grid,
    Focus
};

struct MemberInfo {
    QString id;
    QString name;
    QString role = QStringLiteral("member");
    bool mic = false;
    bool camera = false;
    bool poor = false;
    bool speaking = false;
    bool isSelf = false;
    QString source = QStringLiteral("mic");
};

struct JoinOptions {
    QString server = QStringLiteral("ws://test.example.com:8080");
    QString userId = QStringLiteral("u1001");
    QString roomId = QStringLiteral("review-room");
    QString token;
    QString audioBackend = QStringLiteral("WASAPI (Core Audio)");
    QString modePref = QStringLiteral("auto");
    QString talkMode = QStringLiteral("free");
    Scenario scenario = Scenario::Multi;
    bool fetchDevToken = false;
    QString source = QStringLiteral("mic");
    int volume = 60;
    QString micDevice = QStringLiteral("default");
    QString playoutDevice = QStringLiteral("default");
    QString cameraDevice = QStringLiteral("default");
};

struct UiState {
    bool joined = false;
    QString roomTitle = QStringLiteral("产品评审室");
    QString roomId = QStringLiteral("review-room");
    QString connectionText = QStringLiteral("对讲中");
    QString mediaMode = QStringLiteral("sfu");
    QString talkMode = QStringLiteral("free");
    bool relay = false;
    QVector<MemberInfo> members;
    QString selectedId;
    QString source = QStringLiteral("mic");
    int volume = 60;
    QString micDevice = QStringLiteral("default");
    QString playoutDevice = QStringLiteral("default");
    QString cameraDevice = QStringLiteral("default");
    bool micOn = false;
    bool cameraOn = false;
    bool sharing = false;
    bool deafen = false;
    bool preview = false;
    bool diagnosticsOpen = false;
    bool fetchDevToken = false;
    PanelTab tab = PanelTab::Members;
    StageFilter filter = StageFilter::All;
    StageLayout layout = StageLayout::Grid;
    StageLayout layoutBeforeShare = StageLayout::Grid;
    int elapsed = 0;
    JoinOptions join;
    QStringList logs;
};

inline QString scenarioLabel(Scenario scenario)
{
    switch (scenario) {
    case Scenario::Prejoin: return QStringLiteral("入房配置");
    case Scenario::Multi: return QStringLiteral("多人 SFU");
    case Scenario::One: return QStringLiteral("1v1 P2P");
    case Scenario::Sharing: return QStringLiteral("屏幕共享");
    case Scenario::Devices: return QStringLiteral("设备面板");
    case Scenario::Diagnostics: return QStringLiteral("诊断面板");
    }
    return QStringLiteral("多人 SFU");
}

inline QString modeLabel(const QString &mode)
{
    if (mode == QStringLiteral("p2p")) return QStringLiteral("P2P");
    if (mode == QStringLiteral("sfu")) return QStringLiteral("SFU");
    return QStringLiteral("自动");
}

inline QString talkLabel(const QString &mode)
{
    if (mode == QStringLiteral("ptt")) return QStringLiteral("抢麦 PTT");
    if (mode == QStringLiteral("moderated")) return QStringLiteral("主持人管控");
    return QStringLiteral("自由通话");
}

inline Scenario scenarioFromString(const QString &value)
{
    if (value.compare(QStringLiteral("one"), Qt::CaseInsensitive) == 0) return Scenario::One;
    if (value.compare(QStringLiteral("sharing"), Qt::CaseInsensitive) == 0) return Scenario::Sharing;
    if (value.compare(QStringLiteral("devices"), Qt::CaseInsensitive) == 0) return Scenario::Devices;
    if (value.compare(QStringLiteral("diagnostics"), Qt::CaseInsensitive) == 0) return Scenario::Diagnostics;
    if (value.compare(QStringLiteral("multi"), Qt::CaseInsensitive) == 0) return Scenario::Multi;
    return Scenario::Prejoin;
}

Q_DECLARE_METATYPE(Scenario)
Q_DECLARE_METATYPE(PanelTab)

