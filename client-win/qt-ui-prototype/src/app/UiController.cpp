#include "app/UiController.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

UiController::UiController(QObject *parent)
    : QObject(parent)
{
    m_state.logs.append(QStringLiteral("[原型] 启动：音频优先，视频默认关闭"));
    m_clock.setInterval(1000);
    connect(&m_clock, &QTimer::timeout, this, [this]() {
        if (!m_state.joined) return;
        ++m_state.elapsed;
        emit elapsedChanged(m_state.elapsed);
        if (m_state.talkMode == QStringLiteral("free") && (m_state.elapsed % 7 == 0)) {
            for (MemberInfo &member : m_state.members) {
                if (member.isSelf) continue;
                member.speaking = member.mic && !member.poor && (member.id == QStringLiteral("u1"));
            }
            emit stateChanged();
        }
    });
}

const MemberInfo *UiController::memberById(const QString &id) const
{
    for (const MemberInfo &member : m_state.members) {
        if (member.id == id) return &member;
    }
    return nullptr;
}

const MemberInfo *UiController::selectedMember() const
{
    return memberById(m_state.selectedId);
}

QString UiController::effectiveMediaMode() const
{
    if (m_state.join.modePref == QStringLiteral("sfu")) return QStringLiteral("sfu");
    if (m_state.join.modePref == QStringLiteral("p2p")) return QStringLiteral("p2p");
    return m_state.members.size() <= 2 ? QStringLiteral("p2p") : QStringLiteral("sfu");
}

void UiController::join(const JoinOptions &options)
{
    m_state.join = options;
    m_clock.stop();
    m_state.joined = false;
    m_state.members.clear();
    m_state.selectedId.clear();
    m_state.logs.clear();
    m_state.roomId = options.roomId.isEmpty() ? QStringLiteral("review-room") : options.roomId;
    m_state.talkMode = options.talkMode;
    m_state.source = options.source;
    m_state.volume = options.volume;
    m_state.micDevice = options.micDevice;
    m_state.playoutDevice = options.playoutDevice;
    m_state.cameraDevice = options.cameraDevice;
    m_state.fetchDevToken = options.fetchDevToken;
    m_state.elapsed = 0;
    m_state.micOn = false;
    m_state.cameraOn = false;
    m_state.sharing = false;
    m_state.deafen = false;
    m_state.preview = false;
    m_state.tab = PanelTab::Members;
    m_state.filter = StageFilter::All;
    m_state.layout = StageLayout::Grid;
    m_state.diagnosticsOpen = false;
    seedMembers(options.scenario == Scenario::One ? Scenario::One : Scenario::Multi);
    m_state.mediaMode = effectiveMediaMode();
    m_state.connectionText = QStringLiteral("对讲中");
    m_state.selectedId = m_state.members.size() > 1 ? m_state.members.at(1).id : QStringLiteral("me");
    addLog(QStringLiteral("信令已连接：%1").arg(options.server));
    addLog(QStringLiteral("已加入房间 %1（默认闭麦、摄像头关闭）").arg(m_state.roomId));
    m_clock.start();
    m_state.joined = true;
    emit joined();
    emitChanged();
}

void UiController::leaveRoom()
{
    if (!m_state.joined) return;
    m_clock.stop();
    addLog(QStringLiteral("离开房间 %1").arg(m_state.roomId));
    m_state.joined = false;
    m_state.diagnosticsOpen = false;
    m_state.members.clear();
    emit left();
    emitChanged();
}

void UiController::startScenario(Scenario scenario)
{
    JoinOptions options = m_state.join;
    options.scenario = scenario == Scenario::Prejoin ? Scenario::Multi : scenario;
    if (scenario == Scenario::One) {
        options.modePref = QStringLiteral("p2p");
        options.talkMode = QStringLiteral("free");
    } else {
        options.modePref = QStringLiteral("auto");
        options.talkMode = QStringLiteral("free");
    }
    join(options);

    if (scenario == Scenario::Sharing) {
        toggleShare();
    }
    if (scenario == Scenario::Devices) {
        m_state.tab = PanelTab::Devices;
        emitChanged();
    } else if (scenario == Scenario::Diagnostics) {
        openDiagnostics();
    }
}

void UiController::toggleMic()
{
    m_state.micOn = !m_state.micOn;
    syncSelfMember();
    addLog(QStringLiteral("本地麦克风 → %1").arg(m_state.micOn ? QStringLiteral("开启") : QStringLiteral("关闭")));
    emit toastRequested(m_state.micOn ? QStringLiteral("麦克风已开启") : QStringLiteral("麦克风已关闭"),
                        m_state.micOn ? QStringLiteral("ok") : QStringLiteral("info"));
    emitChanged();
}

void UiController::toggleCamera()
{
    m_state.cameraOn = !m_state.cameraOn;
    syncSelfMember();
    addLog(QStringLiteral("本地摄像头 → %1").arg(m_state.cameraOn ? QStringLiteral("开启") : QStringLiteral("关闭")));
    emit toastRequested(m_state.cameraOn ? QStringLiteral("摄像头已开启（模拟）") : QStringLiteral("摄像头已关闭"),
                        m_state.cameraOn ? QStringLiteral("ok") : QStringLiteral("info"));
    emitChanged();
}

void UiController::toggleShare()
{
    if (!m_state.sharing) {
        m_state.layoutBeforeShare = m_state.layout;
        m_state.sharing = true;
        addLog(QStringLiteral("屏幕共享开始：tracks[].source=screen"));
        emit toastRequested(QStringLiteral("屏幕共享已开始（模拟）"), QStringLiteral("ok"));
    } else {
        m_state.sharing = false;
        m_state.layout = m_state.layoutBeforeShare;
        addLog(QStringLiteral("屏幕共享停止"));
        emit toastRequested(QStringLiteral("屏幕共享已停止"), QStringLiteral("info"));
    }
    emitChanged();
}

void UiController::toggleDeafen()
{
    m_state.deafen = !m_state.deafen;
    if (m_state.deafen && m_state.micOn) {
        m_state.micOn = false;
        syncSelfMember();
    }
    addLog(QStringLiteral("收听静音 → %1").arg(m_state.deafen ? QStringLiteral("开启") : QStringLiteral("关闭")));
    emit toastRequested(m_state.deafen ? QStringLiteral("已静音收听（仅本机生效）") : QStringLiteral("已恢复收听"),
                        m_state.deafen ? QStringLiteral("warn") : QStringLiteral("ok"));
    emitChanged();
}

void UiController::setSource(const QString &source)
{
    if (m_state.source == source) return;
    m_state.source = source;
    syncSelfMember();
    addLog(QStringLiteral("音频采集源 → %1").arg(source == QStringLiteral("system") ? QStringLiteral("系统音") : QStringLiteral("麦克风")));
    emit toastRequested(source == QStringLiteral("system")
                            ? QStringLiteral("采集源已切换为系统音（WASAPI loopback）")
                            : QStringLiteral("采集源已切换为麦克风"),
                        QStringLiteral("ok"));
    emitChanged();
}

void UiController::selectMember(const QString &id)
{
    if (m_state.selectedId == id) return;
    if (!memberById(id)) return;
    m_state.selectedId = id;
    emitChanged();
}

void UiController::setPanelTab(PanelTab tab)
{
    if (m_state.tab == tab) return;
    m_state.tab = tab;
    emitChanged();
}

void UiController::setStageFilter(StageFilter filter)
{
    if (m_state.filter == filter) return;
    m_state.filter = filter;
    emitChanged();
}

void UiController::setStageLayout(StageLayout layout)
{
    if (m_state.sharing || m_state.layout == layout) return;
    m_state.layout = layout;
    emitChanged();
}

void UiController::muteAll()
{
    for (MemberInfo &member : m_state.members) {
        if (member.isSelf) continue;
        member.mic = false;
        member.speaking = false;
    }
    addLog(QStringLiteral("host.muteAll"));
    emit toastRequested(QStringLiteral("已全员静音（除主持人外）"), QStringLiteral("warn"));
    emitChanged();
}

void UiController::memberAction(const QString &id, const QString &action)
{
    MemberInfo *member = nullptr;
    for (MemberInfo &candidate : m_state.members) {
        if (candidate.id == id) {
            member = &candidate;
            break;
        }
    }
    if (!member || member->isSelf) return;

    if (action == QStringLiteral("mute")) {
        member->mic = false;
        member->speaking = false;
        emit toastRequested(QStringLiteral("已强制 %1 闭麦").arg(member->name), QStringLiteral("warn"));
        addLog(QStringLiteral("host.forceMute → %1").arg(member->name));
    } else if (action == QStringLiteral("grant")) {
        member->mic = true;
        member->speaking = true;
        emit toastRequested(QStringLiteral("已授予 %1 发言权").arg(member->name), QStringLiteral("ok"));
        addLog(QStringLiteral("host.grantFloor → %1").arg(member->name));
    } else if (action == QStringLiteral("remove")) {
        const QString name = member->name;
        m_state.members.erase(std::remove_if(m_state.members.begin(), m_state.members.end(),
                                             [id](const MemberInfo &item) { return item.id == id; }),
                              m_state.members.end());
        if (m_state.selectedId == id && !m_state.members.isEmpty()) {
            m_state.selectedId = m_state.members.first().id;
        }
        emit toastRequested(QStringLiteral("已移出成员：%1").arg(name), QStringLiteral("error"));
        addLog(QStringLiteral("host.remove → %1").arg(name));
    }
    emitChanged();
}

void UiController::setMicDevice(const QString &id)
{
    m_state.micDevice = id;
    addLog(QStringLiteral("recording_device → %1").arg(id));
    emit toastRequested(QStringLiteral("麦克风设备已切换（下次开麦生效）"), QStringLiteral("ok"));
    emitChanged();
}

void UiController::setPlayoutDevice(const QString &id)
{
    m_state.playoutDevice = id;
    addLog(QStringLiteral("playout_device → %1").arg(id));
    emit toastRequested(QStringLiteral("播放设备已切换"), QStringLiteral("ok"));
    emitChanged();
}

void UiController::setCameraDevice(const QString &id)
{
    m_state.cameraDevice = id;
    addLog(QStringLiteral("camera_device → %1").arg(id));
    emit toastRequested(QStringLiteral("摄像头设备已切换"), QStringLiteral("ok"));
    emitChanged();
}

void UiController::setVolume(int volume)
{
    m_state.volume = qBound(0, volume, 100);
    emitChanged();
}

void UiController::setPreview(bool enabled)
{
    m_state.preview = enabled;
    emit toastRequested(enabled ? QStringLiteral("本地预览已开启（模拟）") : QStringLiteral("本地预览已关闭"),
                        QStringLiteral("info"));
    emitChanged();
}

void UiController::refreshDevices()
{
    emit toastRequested(QStringLiteral("设备已刷新：3 个采集 / 3 个播放 / 2 个摄像头"), QStringLiteral("ok"));
    addLog(QStringLiteral("audio.devices → 3 个采集，3 个播放"));
}

void UiController::openDiagnostics()
{
    if (m_state.diagnosticsOpen) return;
    m_state.diagnosticsOpen = true;
    addLog(QStringLiteral("打开诊断面板"));
    emit diagnosticsChanged(true);
    emitChanged();
}

void UiController::closeDiagnostics()
{
    if (!m_state.diagnosticsOpen) return;
    m_state.diagnosticsOpen = false;
    emit diagnosticsChanged(false);
    emitChanged();
}

void UiController::setScenario(Scenario scenario)
{
    if (scenario == Scenario::Prejoin) return;
    const bool wasSharing = m_state.sharing;
    m_state.join.scenario = scenario;
    m_state.join.modePref = scenario == Scenario::One ? QStringLiteral("p2p") : QStringLiteral("auto");
    seedMembers(scenario);
    m_state.mediaMode = effectiveMediaMode();
    m_state.tab = PanelTab::Members;
    m_state.layout = StageLayout::Grid;
    m_state.selectedId = m_state.members.size() > 1 ? m_state.members.at(1).id : QString();
    m_state.sharing = wasSharing;
    addLog(QStringLiteral("原型场景切换：%1").arg(scenarioLabel(scenario)));
    emitChanged();
}

void UiController::seedMembers(Scenario scenario)
{
    m_state.members.clear();
    MemberInfo self;
    self.id = QStringLiteral("me");
    self.name = QStringLiteral("我");
    self.role = scenario == Scenario::One
        ? QStringLiteral("member")
        : QStringLiteral("host");
    self.isSelf = true;
    self.mic = m_state.micOn;
    self.camera = m_state.cameraOn;
    self.source = m_state.source;
    m_state.members.append(self);

    if (scenario == Scenario::One) {
        m_state.members.append({QStringLiteral("u1"), QStringLiteral("李四"), QStringLiteral("member"), true, true, false, false, false, QStringLiteral("mic")});
        m_state.selectedId = QStringLiteral("u1");
        return;
    }

    m_state.members.append({QStringLiteral("u1"), QStringLiteral("张三"), QStringLiteral("member"), true, true, false, false, false, QStringLiteral("mic")});
    m_state.members.append({QStringLiteral("u2"), QStringLiteral("李四"), QStringLiteral("member"), true, true, false, false, false, QStringLiteral("mic")});
    m_state.members.append({QStringLiteral("u3"), QStringLiteral("王五"), QStringLiteral("member"), false, false, false, false, false, QStringLiteral("mic")});
    m_state.members.append({QStringLiteral("u4"), QStringLiteral("赵六"), QStringLiteral("member"), true, false, true, false, false, QStringLiteral("mic")});
    m_state.members.append({QStringLiteral("u5"), QStringLiteral("孙七"), QStringLiteral("member"), false, true, false, false, false, QStringLiteral("mic")});
    m_state.selectedId = QStringLiteral("u1");
}

void UiController::syncSelfMember()
{
    for (MemberInfo &member : m_state.members) {
        if (!member.isSelf) continue;
        member.mic = m_state.micOn;
        member.camera = m_state.cameraOn;
        member.source = m_state.source;
        break;
    }
}

void UiController::addLog(const QString &text)
{
    const QString timestamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    m_state.logs.prepend(QStringLiteral("[%1] %2").arg(timestamp, text));
    while (m_state.logs.size() > 40) m_state.logs.removeLast();
}

void UiController::emitChanged()
{
    emit stateChanged();
}

QString UiController::statsJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("ts"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    QJsonObject room;
    room.insert(QStringLiteral("id"), m_state.roomId);
    room.insert(QStringLiteral("members"), m_state.members.size());
    room.insert(QStringLiteral("media_mode"), m_state.mediaMode);
    room.insert(QStringLiteral("talk_mode"), m_state.talkMode);
    room.insert(QStringLiteral("relay"), m_state.relay);
    root.insert(QStringLiteral("room"), room);

    QJsonObject self;
    self.insert(QStringLiteral("source"), m_state.source);
    self.insert(QStringLiteral("mic_device"), m_state.micDevice);
    self.insert(QStringLiteral("playout_device"), m_state.playoutDevice);
    self.insert(QStringLiteral("camera_device"), m_state.cameraDevice);
    self.insert(QStringLiteral("mic"), m_state.micOn);
    self.insert(QStringLiteral("camera"), m_state.cameraOn);
    self.insert(QStringLiteral("sharing"), m_state.sharing);
    self.insert(QStringLiteral("deafen"), m_state.deafen);
    root.insert(QStringLiteral("self"), self);

    QJsonObject stats;
    stats.insert(QStringLiteral("transport"), m_state.mediaMode == QStringLiteral("p2p") ? QStringLiteral("p2p") : QStringLiteral("sfu"));
    stats.insert(QStringLiteral("rtt_ms"), m_state.mediaMode == QStringLiteral("p2p") ? 18 : 36);
    stats.insert(QStringLiteral("jitter_ms"), 2.4);
    stats.insert(QStringLiteral("packet_loss_pct"), 0.12);
    stats.insert(QStringLiteral("audio_bitrate_kbps"), 32);
    stats.insert(QStringLiteral("video_width"), m_state.cameraOn ? 1280 : 0);
    stats.insert(QStringLiteral("video_height"), m_state.cameraOn ? 720 : 0);
    root.insert(QStringLiteral("selected_stream"), stats);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

