#include "pages/RoomPage.h"

#include "widgets/Avatar.h"
#include "widgets/ControlButton.h"
#include "widgets/IconButton.h"
#include "widgets/Pill.h"
#include "widgets/SegmentedControl.h"
#include "video/VideoGrid.h"

#include <QCheckBox>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QScreen>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QLabel *label(const QString &text, const QString &objectName, QWidget *parent = nullptr)
{
    return Theme::label(text, objectName, parent);
}

void clearLayout(QLayout *layout)
{
    if (!layout) return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (QWidget *widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        if (QLayout *childLayout = item->layout()) {
            clearLayout(childLayout);
            childLayout->deleteLater();
        }
        delete item;
    }
}

QString panelTabId(PanelTab tab)
{
    switch (tab) {
    case PanelTab::Members: return QStringLiteral("members");
    case PanelTab::Devices: return QStringLiteral("devices");
    case PanelTab::Status: return QStringLiteral("status");
    }
    return QStringLiteral("members");
}

QVector<QPair<QString, QString>> mockDevices(const QString &kind)
{
    if (kind == QStringLiteral("camera")) {
        return {{QStringLiteral("default"), QStringLiteral("默认摄像头")},
                {QStringLiteral("camera-usb"), QStringLiteral("USB Camera")},
                {QStringLiteral("camera-virtual"), QStringLiteral("OBS Virtual Camera")}};
    }
    if (kind == QStringLiteral("playout")) {
        return {{QStringLiteral("default"), QStringLiteral("系统默认扬声器")},
                {QStringLiteral("speaker-usb"), QStringLiteral("USB Headset")},
                {QStringLiteral("speaker-hdmi"), QStringLiteral("HDMI Output")}};
    }
    return {{QStringLiteral("default"), QStringLiteral("系统默认麦克风")},
            {QStringLiteral("mic-array"), QStringLiteral("麦克风阵列")},
            {QStringLiteral("mic-usb"), QStringLiteral("USB Microphone")}};
}

void makeMouseTransparent(QWidget *widget)
{
    if (widget) widget->setAttribute(Qt::WA_TransparentForMouseEvents, true);
}

QString memberSummary(const MemberInfo &member, bool deafen)
{
    QStringList parts;
    parts << (member.role == QStringLiteral("host") ? QStringLiteral("主持人") : QStringLiteral("成员"));
    parts << (member.mic ? QStringLiteral("麦开") : QStringLiteral("闭麦"));
    parts << (member.camera ? QStringLiteral("视频开") : QStringLiteral("视频关"));
    if (member.poor) parts << QStringLiteral("网络差");
    if (member.speaking) parts << QStringLiteral("发言中");
    if (member.isSelf && deafen) parts << QStringLiteral("本机静音收听");
    return parts.join(QStringLiteral(" · "));
}

bool sameMembers(const QVector<MemberInfo> &left, const QVector<MemberInfo> &right)
{
    if (left.size() != right.size()) return false;
    for (int i = 0; i < left.size(); ++i) {
        const MemberInfo &a = left.at(i);
        const MemberInfo &b = right.at(i);
        if (a.id != b.id || a.name != b.name || a.role != b.role || a.mic != b.mic ||
            a.camera != b.camera || a.poor != b.poor || a.speaking != b.speaking ||
            a.isSelf != b.isSelf || a.source != b.source)
            return false;
    }
    return true;
}

bool needsPanelRebuild(const UiState &before, const UiState &after)
{
    return before.tab != after.tab ||
           before.selectedId != after.selectedId ||
           before.source != after.source ||
           before.micDevice != after.micDevice ||
           before.playoutDevice != after.playoutDevice ||
           before.cameraDevice != after.cameraDevice ||
           before.connectionText != after.connectionText ||
           before.mediaMode != after.mediaMode ||
           before.talkMode != after.talkMode ||
           before.relay != after.relay ||
           before.sharing != after.sharing ||
           before.micOn != after.micOn ||
           before.cameraOn != after.cameraOn ||
           before.deafen != after.deafen ||
           !sameMembers(before.members, after.members);
}
} // namespace

RoomPage::RoomPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("RoomPage"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto *main = new QWidget(this);
    main->setObjectName(QStringLiteral("RoomMain"));
    auto *mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    mainLayout->addWidget(buildHeader());
    mainLayout->addWidget(buildStage(), 1);
    mainLayout->addWidget(buildControlBar());
    root->addWidget(main, 1);
    root->addWidget(buildSidebar());

    m_diagnosticsOverlay = buildDiagnostics();
    m_settingsDialog = buildSettingsDialog();
    m_leaveDialog = buildLeaveDialog();
    m_diagnosticsOverlay->hide();

    updateHeader();
    updateControls();
    updateDiagnostics();
    positionOverlays();
}

QWidget *RoomPage::buildHeader()
{
    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("RoomHeader"));
    header->setFixedHeight(64);
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(22, 0, 126, 0);
    layout->setSpacing(10);

    auto *titleBlock = new QVBoxLayout;
    titleBlock->setContentsMargins(0, 0, 0, 0);
    titleBlock->setSpacing(1);
    auto *titleLine = new QHBoxLayout;
    titleLine->setContentsMargins(0, 0, 0, 0);
    titleLine->setSpacing(9);
    m_roomTitle = label(QStringLiteral("产品评审室"), QStringLiteral("RoomTitle"), header);
    m_roomId = label(QStringLiteral("review-room"), QStringLiteral("RoomId"), header);
    titleLine->addWidget(m_roomTitle);
    titleLine->addWidget(m_roomId, 0, Qt::AlignVCenter);
    titleLine->addStretch(1);
    titleBlock->addLayout(titleLine);

    auto *badges = new QHBoxLayout;
    badges->setContentsMargins(0, 0, 0, 0);
    badges->setSpacing(7);
    m_connectionPill = new Pill(QStringLiteral("对讲中"), QStringLiteral("ok"), header);
    m_connectionPill->setIcon(Theme::Icon::Signal);
    m_hostPill = new Pill(QStringLiteral("主持人"), QStringLiteral("strong"), header);
    m_sharePill = new Pill(QStringLiteral("共享中"), QStringLiteral("accent"), header);
    m_sharePill->setIcon(Theme::Icon::Screen);
    badges->addWidget(m_connectionPill);
    badges->addWidget(m_hostPill);
    badges->addWidget(m_sharePill);
    badges->addStretch(1);
    titleBlock->addLayout(badges);
    layout->addLayout(titleBlock, 1);

    m_modePill = new Pill(QStringLiteral("SFU"), QStringLiteral("strong"), header);
    m_directPill = new Pill(QStringLiteral("直连"), QStringLiteral("default"), header);
    m_memberCountPill = new Pill(QStringLiteral("0 人"), QStringLiteral("default"), header);
    m_memberCountPill->setIcon(Theme::Icon::Users);
    m_videoCountPill = new Pill(QStringLiteral("0/9"), QStringLiteral("default"), header);
    m_videoCountPill->setIcon(Theme::Icon::Camera);
    m_clockPill = new Pill(QStringLiteral("00:00:00"), QStringLiteral("default"), header);
    m_clockPill->setIcon(Theme::Icon::Clock);
    m_prototypePill = new Pill(QStringLiteral("原型 · 多人 SFU"), QStringLiteral("accent"), header);

    layout->addWidget(m_modePill);
    layout->addWidget(m_directPill);
    layout->addWidget(m_memberCountPill);
    layout->addWidget(m_videoCountPill);
    layout->addWidget(m_clockPill);
    layout->addWidget(m_prototypePill);
    return header;
}

QWidget *RoomPage::buildStage()
{
    auto *stage = new QWidget(this);
    stage->setObjectName(QStringLiteral("RoomStage"));
    auto *layout = new QVBoxLayout(stage);
    layout->setContentsMargins(14, 10, 14, 12);
    layout->setSpacing(10);

    auto *bar = new QFrame(stage);
    bar->setObjectName(QStringLiteral("StageBar"));
    bar->setFixedHeight(44);
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(12, 0, 12, 0);
    barLayout->setSpacing(10);

    m_filterControl = new SegmentedControl(true, bar);
    m_filterControl->addItem(QStringLiteral("all"), QStringLiteral("全部成员"));
    m_filterControl->addItem(QStringLiteral("video"), QStringLiteral("仅视频成员"));
    m_filterControl->setCurrentId(QStringLiteral("all"));
    barLayout->addWidget(m_filterControl);
    barLayout->addStretch(1);

    m_floorPill = new Pill(QStringLiteral("自由发言"), QStringLiteral("default"), bar);
    barLayout->addWidget(m_floorPill);

    m_layoutControl = new SegmentedControl(true, bar);
    m_layoutControl->addItem(QStringLiteral("grid"), QStringLiteral("宫格"));
    m_layoutControl->addItem(QStringLiteral("focus"), QStringLiteral("聚焦"));
    m_layoutControl->setCurrentId(QStringLiteral("grid"));
    barLayout->addWidget(m_layoutControl);

    m_videoGrid = new VideoGrid(stage);
    layout->addWidget(bar);
    layout->addWidget(m_videoGrid, 1);

    connect(m_filterControl, &SegmentedControl::currentIdChanged, this, [this](const QString &id) {
        emit stageFilterRequested(id == QStringLiteral("video") ? StageFilter::VideoOnly : StageFilter::All);
    });
    connect(m_layoutControl, &SegmentedControl::currentIdChanged, this, [this](const QString &id) {
        emit stageLayoutRequested(id == QStringLiteral("focus") ? StageLayout::Focus : StageLayout::Grid);
    });
    connect(m_videoGrid, &VideoGrid::memberSelected, this, &RoomPage::memberSelected);
    connect(m_videoGrid, &VideoGrid::memberFocusRequested, this, &RoomPage::memberFocusRequested);
    return stage;
}
QWidget *RoomPage::buildControlBar()
{
    auto *bar = new QFrame(this);
    bar->setObjectName(QStringLiteral("ControlBar"));
    bar->setProperty("noWindowDrag", true);
    bar->setFixedHeight(92);
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(24, 9, 24, 9);
    layout->setSpacing(14);

    auto *left = new QHBoxLayout;
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(12);
    m_micControl = new ControlButton(Theme::Icon::MicOff, QStringLiteral("闭麦"), bar);
    m_cameraControl = new ControlButton(Theme::Icon::CameraOff, QStringLiteral("摄像头"), bar);
    m_shareControl = new ControlButton(Theme::Icon::Screen, QStringLiteral("共享屏幕"), bar);
    left->addWidget(m_micControl);
    left->addWidget(m_cameraControl);
    left->addWidget(m_shareControl);
    layout->addLayout(left);

    layout->addStretch(1);
    m_controlHint = label(QStringLiteral("默认闭麦 · 视频默认关闭 · 设备和音源在右侧面板"),
                          QStringLiteral("ControlHint"), bar);
    m_controlHint->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_controlHint);
    layout->addStretch(1);

    auto *right = new QHBoxLayout;
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(12);
    m_listenControl = new ControlButton(Theme::Icon::Headphones, QStringLiteral("收听"), bar);
    m_settingsControl = new ControlButton(Theme::Icon::Settings, QStringLiteral("设置"), bar);
    m_leaveControl = new ControlButton(Theme::Icon::Leave, QStringLiteral("离开"), bar);
    m_leaveControl->setTone(QStringLiteral("danger"));
    right->addWidget(m_listenControl);
    right->addWidget(m_settingsControl);
    right->addWidget(m_leaveControl);
    layout->addLayout(right);

    connect(m_micControl, &ControlButton::clicked, this, &RoomPage::micToggleRequested);
    connect(m_cameraControl, &ControlButton::clicked, this, &RoomPage::cameraToggleRequested);
    connect(m_shareControl, &ControlButton::clicked, this, &RoomPage::shareToggleRequested);
    connect(m_listenControl, &ControlButton::clicked, this, &RoomPage::deafenToggleRequested);
    connect(m_settingsControl, &ControlButton::clicked, this, &RoomPage::showSettingsDialog);
    connect(m_leaveControl, &ControlButton::clicked, this, &RoomPage::showLeaveConfirmation);
    return bar;
}

QWidget *RoomPage::buildSidebar()
{
    auto *side = new QFrame(this);
    side->setObjectName(QStringLiteral("RoomSidebar"));
    side->setFixedWidth(322);
    auto *layout = new QVBoxLayout(side);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *head = new QFrame(side);
    head->setObjectName(QStringLiteral("SideHead"));
    head->setFixedHeight(58);
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(14, 10, 126, 8);
    headLayout->setSpacing(8);
    m_tabControl = new SegmentedControl(false, head);
    m_tabControl->addItem(QStringLiteral("members"), QStringLiteral("成员"));
    m_tabControl->addItem(QStringLiteral("devices"), QStringLiteral("设备"));
    m_tabControl->addItem(QStringLiteral("status"), QStringLiteral("状态"));
    headLayout->addWidget(m_tabControl, 1);
    layout->addWidget(head);

    m_sideScroll = new QScrollArea(side);
    m_sideScroll->setObjectName(QStringLiteral("RoomSideScroll"));
    m_sideScroll->setWidgetResizable(true);
    m_sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_sideBody = new QWidget(m_sideScroll);
    m_sideBody->setObjectName(QStringLiteral("SideBody"));
    m_sideBodyLayout = new QVBoxLayout(m_sideBody);
    m_sideBodyLayout->setContentsMargins(14, 10, 14, 14);
    m_sideBodyLayout->setSpacing(9);
    m_sideScroll->setWidget(m_sideBody);
    layout->addWidget(m_sideScroll, 1);

    m_sideFoot = new QFrame(side);
    m_sideFoot->setObjectName(QStringLiteral("SideFoot"));
    m_sideFootLayout = new QVBoxLayout(m_sideFoot);
    m_sideFootLayout->setContentsMargins(14, 12, 14, 12);
    m_sideFootLayout->setSpacing(0);
    m_sideFootLayout->addWidget(buildSelfFooter());
    layout->addWidget(m_sideFoot);

    connect(m_tabControl, &SegmentedControl::currentIdChanged, this, [this](const QString &id) {
        const PanelTab tab = id == QStringLiteral("devices") ? PanelTab::Devices
            : id == QStringLiteral("status") ? PanelTab::Status : PanelTab::Members;
        emit panelTabRequested(tab);
    });
    return side;
}

QWidget *RoomPage::buildDevicesPanel()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(9);

    const bool system = m_state.source == QStringLiteral("system");
    auto *head = new QWidget(panel);
    auto *headLayout = new QHBoxLayout(head);
    headLayout->setContentsMargins(0, 0, 0, 2);
    headLayout->setSpacing(6);
    headLayout->addWidget(label(system ? QStringLiteral("音频采集 · 系统音")
                                       : QStringLiteral("音频采集 · 麦克风"),
                                QStringLiteral("PanelHead"), head), 1);
    auto *refresh = new IconButton(Theme::Icon::Refresh, head);
    refresh->setTone(QStringLiteral("ghost"));
    refresh->setDiameter(26);
    refresh->setToolTip(QStringLiteral("刷新设备"));
    auto *diagnostic = new IconButton(Theme::Icon::Diagnostics, head);
    diagnostic->setTone(QStringLiteral("ghost"));
    diagnostic->setDiameter(26);
    diagnostic->setToolTip(QStringLiteral("打开诊断"));
    headLayout->addWidget(refresh);
    headLayout->addWidget(diagnostic);
    layout->addWidget(head);

    layout->addWidget(buildDeviceCard(QStringLiteral("mic"), QStringLiteral("麦克风"),
                                      QStringLiteral("使用所选麦克风设备"), Theme::Icon::Mic));
    layout->addWidget(buildDeviceCard(QStringLiteral("system"), QStringLiteral("系统音（默认播放设备）"),
                                      QStringLiteral("WASAPI loopback，不是立体声混音"), Theme::Icon::Screen));

    layout->addSpacing(3);
    layout->addWidget(label(QStringLiteral("麦克风设备"), QStringLiteral("FieldLabel"), panel));
    m_panelMic = makeDeviceCombo(QStringLiteral("microphone"), m_state.micDevice,
                                 mockDevices(QStringLiteral("microphone")), panel);
    m_panelMic->setEnabled(!system);
    m_panelMic->setToolTip(system ? QStringLiteral("系统音模式不使用麦克风设备")
                                  : QStringLiteral("切换后下次开麦生效"));
    layout->addWidget(m_panelMic);
    auto *sourceHint = label(system
        ? QStringLiteral("系统音采集 Windows 默认播放设备的混音，与下方播放设备无关。")
        : QStringLiteral("仅麦克风源使用该设备；切换后下次开麦生效。"),
        QStringLiteral("Hint"), panel);
    sourceHint->setWordWrap(true);
    layout->addWidget(sourceHint);

    layout->addSpacing(3);
    layout->addWidget(label(QStringLiteral("播放设备"), QStringLiteral("FieldLabel"), panel));
    m_panelPlayout = makeDeviceCombo(QStringLiteral("playout"), m_state.playoutDevice,
                                     mockDevices(QStringLiteral("playout")), panel);
    layout->addWidget(m_panelPlayout);

    auto *volumeHeader = new QHBoxLayout;
    volumeHeader->setContentsMargins(0, 3, 0, 0);
    volumeHeader->addWidget(label(QStringLiteral("播放音量"), QStringLiteral("FieldLabel"), panel), 1);
    m_panelVolumeValue = label(QStringLiteral("%1%").arg(m_state.volume), QStringLiteral("Value"), panel);
    volumeHeader->addWidget(m_panelVolumeValue);
    layout->addLayout(volumeHeader);
    m_panelVolume = new QSlider(Qt::Horizontal, panel);
    m_panelVolume->setRange(0, 100);
    m_panelVolume->setValue(m_state.volume);
    layout->addWidget(m_panelVolume);

    layout->addSpacing(4);
    layout->addWidget(label(QStringLiteral("视频"), QStringLiteral("SectionTitle"), panel));
    layout->addWidget(label(QStringLiteral("摄像头"), QStringLiteral("FieldLabel"), panel));
    m_panelCamera = makeDeviceCombo(QStringLiteral("camera"), m_state.cameraDevice,
                                    mockDevices(QStringLiteral("camera")), panel);
    layout->addWidget(m_panelCamera);

    m_panelPreview = new QCheckBox(QStringLiteral("本地预览"), panel);
    m_panelPreview->setChecked(m_state.preview);
    layout->addWidget(m_panelPreview);

    auto *info = new QFrame(panel);
    info->setObjectName(QStringLiteral("InfoBox"));
    auto *infoLayout = new QHBoxLayout(info);
    infoLayout->setContentsMargins(10, 8, 10, 8);
    infoLayout->setSpacing(8);
    auto *infoIcon = new QLabel(info);
    infoIcon->setPixmap(Theme::pixmap(Theme::Icon::Info, 15, QColor(QStringLiteral("#70d5ff"))));
    infoLayout->addWidget(infoIcon, 0, Qt::AlignTop);
    auto *infoText = label(QStringLiteral("视频默认关闭；单路建议 720p 以内，多人优先 360p。"),
                           QStringLiteral("Hint"), info);
    infoText->setWordWrap(true);
    infoLayout->addWidget(infoText, 1);
    layout->addWidget(info);
    layout->addStretch(1);

    connect(refresh, &IconButton::clicked, this, &RoomPage::refreshDevicesRequested);
    connect(diagnostic, &IconButton::clicked, this, &RoomPage::diagnosticsRequested);
    connect(m_panelVolume, &QSlider::valueChanged, this, [this](int value) {
        if (m_panelVolumeValue) m_panelVolumeValue->setText(QStringLiteral("%1%").arg(value));
        emit volumeChanged(value);
    });
    connect(m_panelPreview, &QCheckBox::toggled, this, &RoomPage::previewChanged);
    return panel;
}
QWidget *RoomPage::buildStatusPanel()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(7);

    auto *head = new QHBoxLayout;
    head->setContentsMargins(0, 0, 0, 2);
    head->addWidget(label(QStringLiteral("连接与媒体"), QStringLiteral("PanelHead"), panel), 1);
    auto *diagnostic = Theme::button(QStringLiteral("诊断"), QStringLiteral("ghost"), panel);
    diagnostic->setFixedHeight(30);
    head->addWidget(diagnostic);
    layout->addLayout(head);

    int videoCount = 0;
    for (const MemberInfo &member : m_state.members)
        if (member.camera) ++videoCount;

    layout->addWidget(makeStatusRow(QStringLiteral("信令"), QStringLiteral("● 已连接"), QStringLiteral("ok")));
    layout->addWidget(makeStatusRow(QStringLiteral("媒体模式"),
        m_state.mediaMode == QStringLiteral("p2p")
            ? QStringLiteral("P2P · %1").arg(m_state.relay ? QStringLiteral("TURN 中继") : QStringLiteral("直连"))
            : QStringLiteral("SFU")));
    layout->addWidget(makeStatusRow(QStringLiteral("房间模式"), talkLabel(m_state.talkMode)));
    layout->addWidget(makeStatusRow(QStringLiteral("采集源"), m_state.source == QStringLiteral("system")
        ? QStringLiteral("系统音（默认播放设备）") : QStringLiteral("麦克风")));
    layout->addWidget(makeStatusRow(QStringLiteral("音频编码"), QStringLiteral("Opus 48 kHz / 32 kbps")));
    layout->addWidget(makeStatusRow(QStringLiteral("视频编码"), m_state.cameraOn
        ? QStringLiteral("H.264 硬编 · 720p") : QStringLiteral("H.264 · 未发布")));
    layout->addWidget(makeStatusRow(QStringLiteral("视频路数"), QStringLiteral("%1 / 9").arg(videoCount)));
    auto *elapsedRow = makeStatusRow(QStringLiteral("加入时长"), Theme::formatDuration(m_state.elapsed));
    m_statusElapsed = elapsedRow->findChild<QLabel *>(QStringLiteral("StatusValue"));
    layout->addWidget(elapsedRow);
    layout->addWidget(makeStatusRow(QStringLiteral("发言权"), talkLabel(m_state.talkMode),
                                    m_state.talkMode == QStringLiteral("free") ? QString() : QStringLiteral("ok")));

    const MemberInfo *selected = nullptr;
    for (const MemberInfo &member : m_state.members) {
        if (member.id == m_state.selectedId) {
            selected = &member;
            break;
        }
    }
    if (!selected && !m_state.members.isEmpty()) selected = &m_state.members.first();
    const QString selectedName = selected ? selected->name : QStringLiteral("未选择成员");
    const bool videoOn = selected && selected->camera;

    layout->addSpacing(7);
    layout->addWidget(label(QStringLiteral("选中流 · %1").arg(selectedName),
                            QStringLiteral("SectionTitle"), panel));

    auto *stream = new QFrame(panel);
    stream->setObjectName(QStringLiteral("StreamCard"));
    auto *streamLayout = new QVBoxLayout(stream);
    streamLayout->setContentsMargins(11, 10, 11, 10);
    streamLayout->setSpacing(6);

    auto *streamHead = new QHBoxLayout;
    streamHead->addWidget(label(selectedName, QStringLiteral("TitleSmall"), stream), 1);
    auto *kind = new Pill(selected && selected->isSelf ? QStringLiteral("本机") : QStringLiteral("远端"),
                          QStringLiteral("default"), stream);
    streamHead->addWidget(kind);
    streamLayout->addLayout(streamHead);

    QJsonObject stats;
    const QJsonObject root = QJsonDocument::fromJson(m_statsJson.toUtf8()).object();
    if (root.value(QStringLiteral("selected_stream")).isObject())
        stats = root.value(QStringLiteral("selected_stream")).toObject();

    if (videoOn) {
        const int width = stats.value(QStringLiteral("video_width")).toInt(1280);
        const int height = stats.value(QStringLiteral("video_height")).toInt(720);
        const int fps = stats.value(QStringLiteral("video_fps")).toInt(30);
        const int videoKbps = stats.value(QStringLiteral("video_bitrate_kbps")).toInt(720);
        streamLayout->addWidget(makeStatusRow(QStringLiteral("分辨率"),
                                              QStringLiteral("%1×%2").arg(width).arg(height)));
        streamLayout->addWidget(makeStatusRow(QStringLiteral("帧率"), QStringLiteral("%1 fps").arg(fps)));
        streamLayout->addWidget(makeStatusRow(QStringLiteral("视频码率"), QStringLiteral("%1 kbps").arg(videoKbps)));
    } else {
        streamLayout->addWidget(label(QStringLiteral("仅音频流：Opus 48 kHz / 32 kbps"),
                                      QStringLiteral("Hint"), stream));
    }

    const int rtt = stats.value(QStringLiteral("rtt_ms")).toInt(m_state.mediaMode == QStringLiteral("p2p") ? 18 : 36);
    const double jitter = stats.value(QStringLiteral("jitter_ms")).toDouble(2.4);
    const double loss = stats.value(QStringLiteral("packet_loss_pct")).toDouble(0.12);
    const int audioKbps = stats.value(QStringLiteral("audio_bitrate_kbps")).toInt(32);
    streamLayout->addWidget(makeStatusRow(QStringLiteral("RTT"), QStringLiteral("%1 ms").arg(rtt)));
    streamLayout->addWidget(makeStatusRow(QStringLiteral("抖动"), QStringLiteral("%1 ms").arg(jitter, 0, 'f', 1)));
    streamLayout->addWidget(makeStatusRow(QStringLiteral("丢包"), QStringLiteral("%1%").arg(loss, 0, 'f', 2),
                                          loss < 1.0 ? QStringLiteral("ok") : QStringLiteral("warn")));
    streamLayout->addWidget(makeStatusRow(QStringLiteral("音频码率"), QStringLiteral("%1 kbps").arg(audioKbps)));
    layout->addWidget(stream);

    auto *warning = new QFrame(panel);
    warning->setObjectName(QStringLiteral("WarningBox"));
    auto *warningLayout = new QHBoxLayout(warning);
    warningLayout->setContentsMargins(10, 8, 10, 8);
    warningLayout->setSpacing(8);
    auto *warningIcon = new QLabel(warning);
    warningIcon->setPixmap(Theme::pixmap(Theme::Icon::Alert, 15, QColor(QStringLiteral("#ffc477"))));
    warningLayout->addWidget(warningIcon, 0, Qt::AlignTop);
    auto *warningText = label(QStringLiteral("测试机带宽约 5 Mbps：视频默认关闭；720p 建议 1–2 路。"),
                              QStringLiteral("Hint"), warning);
    warningText->setWordWrap(true);
    warningLayout->addWidget(warningText, 1);
    layout->addWidget(warning);
    layout->addStretch(1);

    connect(diagnostic, &QPushButton::clicked, this, &RoomPage::diagnosticsRequested);
    return panel;
}
QWidget *RoomPage::buildSelfFooter()
{
    auto *footer = new QWidget(this);
    auto *layout = new QHBoxLayout(footer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);

    const MemberInfo *self = nullptr;
    for (const MemberInfo &member : m_state.members) {
        if (member.isSelf) {
            self = &member;
            break;
        }
    }
    auto *avatar = new Avatar(self ? self->name : QStringLiteral("我"), 36, footer);
    layout->addWidget(avatar);

    auto *identity = new QVBoxLayout;
    identity->setContentsMargins(0, 0, 0, 0);
    identity->setSpacing(1);
    const QString name = self ? self->name : QStringLiteral("我");
    const QString role = self && self->role == QStringLiteral("host") ? QStringLiteral("主持人")
                                                                   : QStringLiteral("成员");
    identity->addWidget(label(QStringLiteral("%1 · %2").arg(name, role),
                              QStringLiteral("TitleSmall"), footer));
    QStringList parts;
    parts << (m_state.source == QStringLiteral("system") ? QStringLiteral("系统音") : QStringLiteral("麦克风"));
    parts << (self && self->mic ? QStringLiteral("开麦") : QStringLiteral("闭麦"));
    parts << (self && self->camera ? QStringLiteral("视频开") : QStringLiteral("视频关"));
    if (m_state.deafen) parts << QStringLiteral("静音收听");
    identity->addWidget(label(parts.join(QStringLiteral(" · ")), QStringLiteral("Hint"), footer));
    layout->addLayout(identity, 1);

    auto *mic = new QLabel(footer);
    mic->setPixmap(Theme::pixmap(self && self->mic ? Theme::Icon::Mic : Theme::Icon::MicOff, 16,
                                self && self->mic ? QColor(QStringLiteral("#e8edf2"))
                                                  : QColor(QStringLiteral("#ff7b7b"))));
    auto *camera = new QLabel(footer);
    camera->setPixmap(Theme::pixmap(self && self->camera ? Theme::Icon::Camera : Theme::Icon::CameraOff, 16,
                                   self && self->camera ? QColor(QStringLiteral("#e8edf2"))
                                                        : QColor(QStringLiteral("#ff7b7b"))));
    layout->addWidget(mic);
    layout->addWidget(camera);
    return footer;
}
QWidget *RoomPage::buildDeviceCard(const QString &sourceId, const QString &title,
                                   const QString &description, Theme::Icon icon)
{
    auto *card = new QFrame(this);
    card->setObjectName(QStringLiteral("SourceCard"));
    card->setProperty("sourceId", sourceId);
    card->setProperty("selected", sourceId == m_state.source);
    card->setProperty("noWindowDrag", true);
    card->setCursor(Qt::PointingHandCursor);
    card->installEventFilter(this);

    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(12, 10, 12, 10);
    layout->setSpacing(10);

    auto *iconLabel = new QLabel(card);
    iconLabel->setPixmap(Theme::pixmap(icon, 18, QColor(QStringLiteral("#70d5ff"))));
    makeMouseTransparent(iconLabel);
    layout->addWidget(iconLabel);

    auto *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(1);
    auto *titleLabel = label(title, QStringLiteral("TitleSmall"), card);
    makeMouseTransparent(titleLabel);
    auto *descriptionLabel = label(description, QStringLiteral("Hint"), card);
    makeMouseTransparent(descriptionLabel);
    text->addWidget(titleLabel);
    text->addWidget(descriptionLabel);
    layout->addLayout(text, 1);

    if (sourceId == m_state.source) {
        auto *check = new QLabel(card);
        check->setPixmap(Theme::pixmap(Theme::Icon::Check, 16, QColor(QStringLiteral("#55c2ff"))));
        makeMouseTransparent(check);
        layout->addWidget(check);
    }
    return card;
}
QDialog *RoomPage::buildSettingsDialog()
{
    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("SettingsDialog"));
    dialog->setWindowTitle(QStringLiteral("音视频设置"));
    dialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog->setAttribute(Qt::WA_TranslucentBackground, true);
    dialog->setModal(true);

    auto *outer = new QVBoxLayout(dialog);
    outer->setContentsMargins(22, 22, 22, 22);
    auto *surface = new QFrame(dialog);
    surface->setObjectName(QStringLiteral("SettingsSurface"));
    auto *shadow = new QGraphicsDropShadowEffect(surface);
    shadow->setBlurRadius(38);
    shadow->setOffset(0, 10);
    shadow->setColor(QColor(0, 0, 0, 165));
    surface->setGraphicsEffect(shadow);
    outer->addWidget(surface);

    auto *layout = new QVBoxLayout(surface);
    layout->setContentsMargins(22, 18, 22, 20);
    layout->setSpacing(12);

    auto *head = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(2);
    titles->addWidget(label(QStringLiteral("音视频设置"), QStringLiteral("DialogTitle"), surface));
    titles->addWidget(label(QStringLiteral("当前为 mock 设置，修改会即时同步到房间界面。"),
                            QStringLiteral("Hint"), surface));
    head->addLayout(titles, 1);
    auto *close = new IconButton(Theme::Icon::Close, surface);
    close->setTone(QStringLiteral("ghost"));
    close->setDiameter(32);
    head->addWidget(close, 0, Qt::AlignTop);
    layout->addLayout(head);

    auto *separator = new QFrame(surface);
    separator->setObjectName(QStringLiteral("DialogSeparator"));
    separator->setFixedHeight(1);
    layout->addWidget(separator);

    layout->addWidget(label(QStringLiteral("音频源"), QStringLiteral("FieldLabel"), surface));
    m_settingsSource = new SegmentedControl(false, surface);
    m_settingsSource->addItem(QStringLiteral("mic"), QStringLiteral("麦克风"));
    m_settingsSource->addItem(QStringLiteral("system"), QStringLiteral("系统音频"));
    layout->addWidget(m_settingsSource);

    layout->addWidget(label(QStringLiteral("麦克风设备"), QStringLiteral("FieldLabel"), surface));
    m_settingsMic = makeDeviceCombo(QStringLiteral("microphone"), m_state.micDevice,
                                    mockDevices(QStringLiteral("microphone")), surface);
    layout->addWidget(m_settingsMic);

    layout->addWidget(label(QStringLiteral("播放设备"), QStringLiteral("FieldLabel"), surface));
    m_settingsPlayout = makeDeviceCombo(QStringLiteral("playout"), m_state.playoutDevice,
                                       mockDevices(QStringLiteral("playout")), surface);
    layout->addWidget(m_settingsPlayout);

    auto *volumeHeader = new QHBoxLayout;
    volumeHeader->setContentsMargins(0, 0, 0, 0);
    volumeHeader->addWidget(label(QStringLiteral("播放音量"), QStringLiteral("FieldLabel"), surface), 1);
    m_settingsVolumeValue = label(QStringLiteral("%1%").arg(m_state.volume), QStringLiteral("Value"), surface);
    volumeHeader->addWidget(m_settingsVolumeValue);
    layout->addLayout(volumeHeader);
    m_settingsVolume = new QSlider(Qt::Horizontal, surface);
    m_settingsVolume->setRange(0, 100);
    m_settingsVolume->setValue(m_state.volume);
    layout->addWidget(m_settingsVolume);

    layout->addWidget(label(QStringLiteral("摄像头"), QStringLiteral("FieldLabel"), surface));
    m_settingsCamera = makeDeviceCombo(QStringLiteral("camera"), m_state.cameraDevice,
                                       mockDevices(QStringLiteral("camera")), surface);
    layout->addWidget(m_settingsCamera);

    auto *preview = new QCheckBox(QStringLiteral("开启摄像头预览（mock）"), surface);
    preview->setChecked(m_state.preview);
    layout->addWidget(preview);
    layout->addStretch(1);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 4, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto *cancel = Theme::button(QStringLiteral("取消"), QStringLiteral("ghost"), surface);
    auto *done = Theme::button(QStringLiteral("完成"), QStringLiteral("primary"), surface);
    cancel->setMinimumWidth(88);
    done->setMinimumWidth(96);
    buttons->addWidget(cancel);
    buttons->addWidget(done);
    layout->addLayout(buttons);

    connect(close, &IconButton::clicked, dialog, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(done, &QPushButton::clicked, dialog, &QDialog::accept);
    connect(m_settingsSource, &SegmentedControl::currentIdChanged, this, &RoomPage::sourceRequested);
    connect(m_settingsVolume, &QSlider::valueChanged, this, [this](int value) {
        if (m_settingsVolumeValue) m_settingsVolumeValue->setText(QStringLiteral("%1%").arg(value));
        emit volumeChanged(value);
    });
    connect(preview, &QCheckBox::toggled, this, &RoomPage::previewChanged);

    dialog->resize(590, 650);
    return dialog;
}

QDialog *RoomPage::buildLeaveDialog()
{
    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("LeaveDialog"));
    dialog->setWindowTitle(QStringLiteral("离开房间"));
    dialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog->setAttribute(Qt::WA_TranslucentBackground, true);
    dialog->setModal(true);

    auto *outer = new QVBoxLayout(dialog);
    outer->setContentsMargins(22, 22, 22, 22);
    auto *surface = new QFrame(dialog);
    surface->setObjectName(QStringLiteral("LeaveSurface"));
    auto *shadow = new QGraphicsDropShadowEffect(surface);
    shadow->setBlurRadius(38);
    shadow->setOffset(0, 10);
    shadow->setColor(QColor(0, 0, 0, 165));
    surface->setGraphicsEffect(shadow);
    outer->addWidget(surface);

    auto *layout = new QVBoxLayout(surface);
    layout->setContentsMargins(22, 18, 22, 20);
    layout->setSpacing(12);

    auto *head = new QHBoxLayout;
    head->addWidget(label(QStringLiteral("离开房间"), QStringLiteral("DialogTitle"), surface), 1);
    auto *close = new IconButton(Theme::Icon::Close, surface);
    close->setTone(QStringLiteral("ghost"));
    close->setDiameter(32);
    head->addWidget(close);
    layout->addLayout(head);

    auto *messageRow = new QHBoxLayout;
    messageRow->setContentsMargins(0, 8, 0, 8);
    messageRow->setSpacing(14);
    auto *icon = new QLabel(surface);
    icon->setPixmap(Theme::pixmap(Theme::Icon::Alert, 28, QColor(QStringLiteral("#ffb454"))));
    icon->setFixedSize(32, 32);
    icon->setAlignment(Qt::AlignCenter);
    messageRow->addWidget(icon, 0, Qt::AlignTop);
    auto *message = new QVBoxLayout;
    message->setContentsMargins(0, 0, 0, 0);
    message->setSpacing(4);
    message->addWidget(label(QStringLiteral("确认离开当前房间？"), QStringLiteral("TitleSmall"), surface));
    auto *hint = label(QStringLiteral("离开后本次 mock 会话将结束，并返回入房配置页。"),
                       QStringLiteral("Hint"), surface);
    hint->setWordWrap(true);
    message->addWidget(hint);
    messageRow->addLayout(message, 1);
    layout->addLayout(messageRow);
    layout->addStretch(1);

    auto *buttons = new QHBoxLayout;
    buttons->setContentsMargins(0, 0, 0, 0);
    buttons->setSpacing(8);
    buttons->addStretch(1);
    auto *cancel = Theme::button(QStringLiteral("取消"), QStringLiteral("ghost"), surface);
    auto *confirm = Theme::button(QStringLiteral("确认离开"), QStringLiteral("danger"), surface);
    cancel->setMinimumWidth(88);
    confirm->setMinimumWidth(104);
    buttons->addWidget(cancel);
    buttons->addWidget(confirm);
    layout->addLayout(buttons);

    connect(close, &IconButton::clicked, dialog, &QDialog::reject);
    connect(cancel, &QPushButton::clicked, dialog, &QDialog::reject);
    connect(confirm, &QPushButton::clicked, dialog, &QDialog::accept);
    dialog->resize(480, 260);
    return dialog;
}
QWidget *RoomPage::buildDiagnostics()
{
    auto *overlay = new QWidget(this);
    overlay->setObjectName(QStringLiteral("DiagnosticsOverlay"));
    overlay->setProperty("noWindowDrag", true);
    overlay->setAttribute(Qt::WA_StyledBackground, true);
    overlay->installEventFilter(this);

    auto *layout = new QHBoxLayout(overlay);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addStretch(1);

    auto *drawer = new QFrame(overlay);
    m_diagnosticsDrawer = drawer;
    drawer->setObjectName(QStringLiteral("DiagnosticsDrawer"));
    drawer->setProperty("noWindowDrag", true);
    drawer->setAttribute(Qt::WA_StyledBackground, true);
    drawer->setFixedWidth(540);
    drawer->installEventFilter(this);
    auto *shadow = new QGraphicsDropShadowEffect(drawer);
    shadow->setBlurRadius(36);
    shadow->setOffset(-10, 0);
    shadow->setColor(QColor(0, 0, 0, 160));
    drawer->setGraphicsEffect(shadow);

    auto *drawerLayout = new QVBoxLayout(drawer);
    drawerLayout->setContentsMargins(24, 22, 24, 22);
    drawerLayout->setSpacing(12);

    auto *head = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(2);
    titles->addWidget(label(QStringLiteral("诊断"), QStringLiteral("DialogTitle"), drawer));
    titles->addWidget(label(QStringLiteral("运行日志与选中流统计，仅供排障使用"),
                            QStringLiteral("Hint"), drawer));
    head->addLayout(titles, 1);
    auto *mock = new Pill(QStringLiteral("MOCK"), QStringLiteral("accent"), drawer);
    head->addWidget(mock, 0, Qt::AlignTop);
    auto *close = new IconButton(Theme::Icon::Close, drawer);
    close->setTone(QStringLiteral("ghost"));
    close->setDiameter(32);
    head->addWidget(close, 0, Qt::AlignTop);
    drawerLayout->addLayout(head);

    auto *separator = new QFrame(drawer);
    separator->setObjectName(QStringLiteral("DialogSeparator"));
    separator->setFixedHeight(1);
    drawerLayout->addWidget(separator);

    drawerLayout->addWidget(label(QStringLiteral("运行日志"), QStringLiteral("SectionTitle"), drawer));
    m_diagnosticsLog = new QTextEdit(drawer);
    m_diagnosticsLog->setObjectName(QStringLiteral("DiagnosticsText"));
    m_diagnosticsLog->setReadOnly(true);
    m_diagnosticsLog->setMinimumHeight(220);
    drawerLayout->addWidget(m_diagnosticsLog, 1);

    drawerLayout->addWidget(label(QStringLiteral("选中流 getStats"), QStringLiteral("SectionTitle"), drawer));
    m_diagnosticsStats = new QTextEdit(drawer);
    m_diagnosticsStats->setObjectName(QStringLiteral("DiagnosticsText"));
    m_diagnosticsStats->setReadOnly(true);
    m_diagnosticsStats->setMinimumHeight(220);
    drawerLayout->addWidget(m_diagnosticsStats, 1);

    auto *footer = label(QStringLiteral("提示：此面板在正式版本中仅对排障角色开放。"),
                         QStringLiteral("Hint"), drawer);
    drawerLayout->addWidget(footer);

    layout->addWidget(drawer);
    connect(close, &IconButton::clicked, this, &RoomPage::diagnosticsClosed);
    return overlay;
}
QWidget *RoomPage::buildMemberRow(const MemberInfo &member)
{
    bool selfIsHost = false;
    for (const MemberInfo &candidate : m_state.members) {
        if (candidate.isSelf && candidate.role == QStringLiteral("host")) {
            selfIsHost = true;
            break;
        }
    }

    auto *row = new QFrame(this);
    row->setObjectName(QStringLiteral("MemberRow"));
    row->setProperty("selected", member.id == m_state.selectedId);
    row->setProperty("speaking", member.speaking);
    row->setProperty("poor", member.poor);
    row->setProperty("memberId", member.id);
    row->setProperty("noWindowDrag", true);
    row->setCursor(Qt::PointingHandCursor);
    row->setToolTip(QStringLiteral("单击选择，双击聚焦"));
    row->installEventFilter(this);

    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(10, 8, 8, 8);
    layout->setSpacing(9);

    auto *avatar = new Avatar(member.name, 34, row);
    makeMouseTransparent(avatar);
    layout->addWidget(avatar);

    auto *identity = new QVBoxLayout;
    identity->setContentsMargins(0, 0, 0, 0);
    identity->setSpacing(1);
    QString name = member.name;
    if (member.isSelf) name += QStringLiteral("（我）");
    auto *nameLabel = label(name, QStringLiteral("TitleSmall"), row);
    makeMouseTransparent(nameLabel);
    auto *summary = label(memberSummary(member, member.isSelf && m_state.deafen),
                          QStringLiteral("Hint"), row);
    summary->setWordWrap(false);
    makeMouseTransparent(summary);
    identity->addWidget(nameLabel);
    identity->addWidget(summary);
    layout->addLayout(identity, 1);

    if (!member.mic) {
        auto *mic = new QLabel(row);
        mic->setPixmap(Theme::pixmap(Theme::Icon::MicOff, 14, QColor(QStringLiteral("#ff7b7b"))));
        makeMouseTransparent(mic);
        layout->addWidget(mic);
    }
    if (!member.camera) {
        auto *camera = new QLabel(row);
        camera->setPixmap(Theme::pixmap(Theme::Icon::CameraOff, 14, QColor(QStringLiteral("#ff7b7b"))));
        makeMouseTransparent(camera);
        layout->addWidget(camera);
    }

    if (selfIsHost && !member.isSelf) {
        auto *menuButton = new IconButton(Theme::Icon::More, row);
        menuButton->setTone(QStringLiteral("ghost"));
        menuButton->setDiameter(26);
        menuButton->setToolTip(QStringLiteral("成员操作"));
        menuButton->setProperty("noWindowDrag", true);
        layout->addWidget(menuButton);
        connect(menuButton, &IconButton::clicked, this, [this, row, menuButton, member] {
            QMenu menu(row);
            QAction *mute = menu.addAction(QStringLiteral("强制闭麦"));
            QAction *grant = menu.addAction(QStringLiteral("授予发言权"));
            menu.addSeparator();
            QAction *remove = menu.addAction(QStringLiteral("移出房间"));
            QAction *chosen = menu.exec(menuButton->mapToGlobal(QPoint(0, menuButton->height())));
            if (chosen == mute) emit memberActionRequested(member.id, QStringLiteral("mute"));
            else if (chosen == grant) emit memberActionRequested(member.id, QStringLiteral("grant"));
            else if (chosen == remove) emit memberActionRequested(member.id, QStringLiteral("remove"));
        });
    }
    return row;
}
QWidget *RoomPage::makeStatusRow(const QString &labelText, const QString &value, const QString &tone)
{
    auto *row = new QWidget(this);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 3, 0, 3);
    layout->setSpacing(12);
    layout->addWidget(label(labelText, QStringLiteral("Muted"), row), 1);
    auto *valueLabel = label(value, QStringLiteral("StatusValue"), row);
    valueLabel->setProperty("tone", tone);
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(valueLabel);
    return row;
}

QComboBox *RoomPage::makeDeviceCombo(const QString &kind, const QString &current,
                                     const QVector<QPair<QString, QString>> &items, QWidget *parent)
{
    auto *combo = new QComboBox(parent);
    combo->setProperty("deviceKind", kind);
    for (const auto &item : items) combo->addItem(item.second, item.first);
    const int index = combo->findData(current);
    if (index >= 0) combo->setCurrentIndex(index);
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, combo](int) {
        emit deviceChanged(combo->property("deviceKind").toString(), combo->currentData().toString());
    });
    return combo;
}

void RoomPage::rebuildSidePanel()
{
    if (!m_sideBodyLayout) return;
    const int scrollValue = m_sideScroll ? m_sideScroll->verticalScrollBar()->value() : 0;

    m_panelMic = nullptr;
    m_panelPlayout = nullptr;
    m_panelCamera = nullptr;
    m_panelVolume = nullptr;
    m_panelVolumeValue = nullptr;
    m_panelPreview = nullptr;
    m_statusElapsed = nullptr;
    clearLayout(m_sideBodyLayout);

    bool stretchAdded = false;
    if (m_state.tab == PanelTab::Members) {
        int videoCount = 0;
        bool selfIsHost = false;
        for (const MemberInfo &member : m_state.members) {
            if (member.camera) ++videoCount;
            if (member.isSelf && member.role == QStringLiteral("host")) selfIsHost = true;
        }

        auto *head = new QWidget(m_sideBody);
        auto *headLayout = new QHBoxLayout(head);
        headLayout->setContentsMargins(0, 0, 0, 2);
        headLayout->addWidget(label(QStringLiteral("房间成员 · %1").arg(m_state.members.size()),
                                    QStringLiteral("PanelHead"), head), 1);
        headLayout->addWidget(label(QStringLiteral("%1 路视频").arg(videoCount),
                                    QStringLiteral("Muted"), head));
        m_sideBodyLayout->addWidget(head);

        auto *floor = label(m_state.talkMode == QStringLiteral("free")
                                ? QStringLiteral("自由发言（全双工）")
                                : QStringLiteral("发言权：%1").arg(talkLabel(m_state.talkMode)),
                            QStringLiteral("FloorLine"), m_sideBody);
        m_sideBodyLayout->addWidget(floor);

        for (const MemberInfo &member : m_state.members)
            m_sideBodyLayout->addWidget(buildMemberRow(member));
        m_sideBodyLayout->addStretch(1);
        stretchAdded = true;

        if (selfIsHost) {
            auto *muteAll = Theme::button(QStringLiteral("全员静音"), QStringLiteral("danger"), m_sideBody);
            muteAll->setIcon(QIcon(Theme::pixmap(Theme::Icon::MicOff, 15, QColor(QStringLiteral("#ff9a9a")))));
            muteAll->setIconSize(QSize(15, 15));
            connect(muteAll, &QPushButton::clicked, this, &RoomPage::muteAllRequested);
            m_sideBodyLayout->addWidget(muteAll);
        }
    } else if (m_state.tab == PanelTab::Devices) {
        m_sideBodyLayout->addWidget(buildDevicesPanel());
    } else {
        m_sideBodyLayout->addWidget(buildStatusPanel());
    }
    if (!stretchAdded) m_sideBodyLayout->addStretch(1);

    clearLayout(m_sideFootLayout);
    m_sideFootLayout->addWidget(buildSelfFooter());
    if (m_sideScroll) {
        QTimer::singleShot(0, m_sideScroll, [scroll = m_sideScroll, scrollValue] {
            scroll->verticalScrollBar()->setValue(scrollValue);
        });
    }
}
void RoomPage::updateHeader()
{
    if (!m_roomTitle) return;
    m_roomTitle->setText(m_state.roomTitle);
    m_roomId->setText(m_state.roomId);
    m_connectionPill->setText(m_state.connectionText);
    m_connectionPill->setTone(m_state.connectionText == QStringLiteral("对讲中")
                                  ? QStringLiteral("ok") : QStringLiteral("warn"));

    bool selfIsHost = false;
    int videoCount = 0;
    for (const MemberInfo &member : m_state.members) {
        if (member.isSelf && member.role == QStringLiteral("host")) selfIsHost = true;
        if (member.camera) ++videoCount;
    }
    m_hostPill->setVisible(selfIsHost);
    m_sharePill->setVisible(m_state.sharing);
    m_modePill->setText(modeLabel(m_state.mediaMode));
    m_directPill->setText(m_state.relay ? QStringLiteral("中继") : QStringLiteral("直连"));
    m_directPill->setTone(m_state.relay ? QStringLiteral("warn") : QStringLiteral("default"));
    m_memberCountPill->setText(QStringLiteral("%1 人").arg(m_state.members.size()));
    m_videoCountPill->setText(QStringLiteral("%1/9").arg(videoCount));
    m_clockPill->setText(Theme::formatDuration(m_state.elapsed));
    m_prototypePill->setText(QStringLiteral("原型 · %1").arg(scenarioLabel(m_state.join.scenario)));
    m_floorPill->setText(talkLabel(m_state.talkMode));
}

void RoomPage::updateControls()
{
    if (!m_micControl) return;
    m_micControl->setIcon(m_state.micOn ? Theme::Icon::Mic : Theme::Icon::MicOff);
    m_micControl->setText(m_state.micOn ? QStringLiteral("麦克风") : QStringLiteral("闭麦"));
    m_micControl->setTone(m_state.micOn ? QStringLiteral("active") : QStringLiteral("muted"));

    m_cameraControl->setIcon(m_state.cameraOn ? Theme::Icon::Camera : Theme::Icon::CameraOff);
    m_cameraControl->setText(m_state.cameraOn ? QStringLiteral("摄像头") : QStringLiteral("摄像头关闭"));
    m_cameraControl->setTone(m_state.cameraOn ? QStringLiteral("active") : QStringLiteral("muted"));

    m_shareControl->setIcon(Theme::Icon::Screen);
    m_shareControl->setText(m_state.sharing ? QStringLiteral("停止共享") : QStringLiteral("共享屏幕"));
    m_shareControl->setTone(m_state.sharing ? QStringLiteral("active") : QStringLiteral("default"));

    m_listenControl->setIcon(m_state.deafen ? Theme::Icon::HeadphonesOff : Theme::Icon::Headphones);
    m_listenControl->setText(m_state.deafen ? QStringLiteral("已静音") : QStringLiteral("收听"));
    m_listenControl->setTone(m_state.deafen ? QStringLiteral("muted") : QStringLiteral("default"));

    if (m_controlHint) {
        m_controlHint->setText(m_state.sharing
            ? QStringLiteral("正在共享屏幕 · 宫格与聚焦布局暂时不可用")
            : QStringLiteral("默认闭麦 · 视频默认关闭 · 设备和音源在右侧面板"));
    }
    if (m_filterControl)
        m_filterControl->setCurrentId(m_state.filter == StageFilter::VideoOnly
                                          ? QStringLiteral("video") : QStringLiteral("all"));
    if (m_layoutControl) {
        m_layoutControl->setCurrentId(m_state.layout == StageLayout::Focus
                                          ? QStringLiteral("focus") : QStringLiteral("grid"));
        m_layoutControl->setItemEnabled(QStringLiteral("grid"), !m_state.sharing);
        m_layoutControl->setItemEnabled(QStringLiteral("focus"), !m_state.sharing);
    }
    if (m_tabControl) m_tabControl->setCurrentId(panelTabId(m_state.tab));
}

void RoomPage::updateDiagnostics()
{
    if (m_diagnosticsLog) {
        m_diagnosticsLog->setPlainText(m_state.logs.isEmpty()
            ? QStringLiteral("暂无诊断日志")
            : m_state.logs.join(QLatin1Char('\n')));
    }
    if (m_diagnosticsStats) {
        m_diagnosticsStats->setPlainText(m_statsJson.isEmpty()
            ? QStringLiteral("暂无统计数据")
            : m_statsJson);
    }
}

void RoomPage::syncSettingsControls()
{
    if (m_settingsSource)
        m_settingsSource->setCurrentId(m_state.source == QStringLiteral("system")
                                           ? QStringLiteral("system") : QStringLiteral("mic"));
    const auto syncCombo = [](QComboBox *combo, const QString &value) {
        if (!combo) return;
        const int index = combo->findData(value);
        if (index >= 0) combo->setCurrentIndex(index);
    };
    syncCombo(m_settingsMic, m_state.micDevice);
    syncCombo(m_settingsPlayout, m_state.playoutDevice);
    syncCombo(m_settingsCamera, m_state.cameraDevice);
    if (m_settingsVolume) m_settingsVolume->setValue(m_state.volume);
    if (m_settingsVolumeValue) m_settingsVolumeValue->setText(QStringLiteral("%1%").arg(m_state.volume));
    if (m_settingsMic) m_settingsMic->setEnabled(m_state.source != QStringLiteral("system"));
}

void RoomPage::positionOverlays()
{
    if (m_diagnosticsOverlay) m_diagnosticsOverlay->setGeometry(rect());
    if (m_settingsDialog && m_settingsDialog->isVisible()) centerDialog(m_settingsDialog);
    if (m_leaveDialog && m_leaveDialog->isVisible()) centerDialog(m_leaveDialog);
}
void RoomPage::setElapsed(int seconds)
{
    m_state.elapsed = seconds;
    if (m_clockPill) m_clockPill->setText(Theme::formatDuration(seconds));
    if (m_state.tab == PanelTab::Status && m_statusElapsed)
        m_statusElapsed->setText(Theme::formatDuration(seconds));
}

void RoomPage::setState(const UiState &state)
{
    const UiState before = m_state;
    const bool rebuildPanel = needsPanelRebuild(before, state);
    m_state = state;

    updateHeader();
    updateControls();
    if (m_videoGrid) {
        m_videoGrid->setMembers(m_state.members, QStringLiteral("me"));
        m_videoGrid->setSelectedId(m_state.selectedId);
        m_videoGrid->setFilter(m_state.filter);
        m_videoGrid->setLayoutMode(m_state.layout);
        m_videoGrid->setSharing(m_state.sharing);
    }

    if (rebuildPanel) {
        QTimer::singleShot(0, this, &RoomPage::rebuildSidePanel);
        return;
    }

    const auto syncCombo = [](QComboBox *combo, const QString &value) {
        if (!combo) return;
        const int index = combo->findData(value);
        if (index < 0) return;
        const QSignalBlocker blocker(combo);
        combo->setCurrentIndex(index);
    };
    syncCombo(m_panelMic, m_state.micDevice);
    syncCombo(m_panelPlayout, m_state.playoutDevice);
    syncCombo(m_panelCamera, m_state.cameraDevice);
    if (m_panelVolume) {
        const QSignalBlocker blocker(m_panelVolume);
        m_panelVolume->setValue(m_state.volume);
    }
    if (m_panelVolumeValue) m_panelVolumeValue->setText(QStringLiteral("%1%").arg(m_state.volume));
    if (m_panelPreview) {
        const QSignalBlocker blocker(m_panelPreview);
        m_panelPreview->setChecked(m_state.preview);
    }
    if (m_settingsDialog && m_settingsDialog->isVisible()) syncSettingsControls();
}

void RoomPage::setStatsJson(const QString &json)
{
    m_statsJson = json;
    updateDiagnostics();
    if (m_state.tab == PanelTab::Status)
        QTimer::singleShot(0, this, &RoomPage::rebuildSidePanel);
}
void RoomPage::setDiagnosticsOpen(bool open)
{
    m_diagnosticsOpen = open;
    if (!m_diagnosticsOverlay) return;
    if (open) {
        updateDiagnostics();
        positionOverlays();
        m_diagnosticsOverlay->show();
        m_diagnosticsOverlay->raise();
    } else {
        m_diagnosticsOverlay->hide();
    }
}

void RoomPage::showSettingsDialog()
{
    if (!m_settingsDialog) return;
    syncSettingsControls();
    emit settingsRequested();
    centerDialog(m_settingsDialog);
    m_settingsDialog->exec();
}

void RoomPage::showLeaveConfirmation()
{
    if (!m_leaveDialog) return;
    centerDialog(m_leaveDialog);
    if (m_leaveDialog->exec() == QDialog::Accepted) emit leaveRequested();
}

void RoomPage::centerDialog(QDialog *dialog)
{
    if (!dialog) return;
    const QSize dialogSize = dialog->size();
    QPoint topLeft = mapToGlobal(rect().center()) - QPoint(dialogSize.width() / 2, dialogSize.height() / 2);
    if (QScreen *currentScreen = screen()) {
        const QRect available = currentScreen->availableGeometry();
        topLeft.setX(qBound(available.left(), topLeft.x(), available.right() - dialogSize.width() + 1));
        topLeft.setY(qBound(available.top(), topLeft.y(), available.bottom() - dialogSize.height() + 1));
    }
    dialog->move(topLeft);
}
bool RoomPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_diagnosticsOverlay && event->type() == QEvent::MouseButtonPress) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            emit diagnosticsClosed();
            return true;
        }
    }

    if (watched == m_diagnosticsDrawer && event->type() == QEvent::MouseButtonPress)
        return true;
    if (auto *frame = qobject_cast<QFrame *>(watched)) {
        if (frame->objectName() == QStringLiteral("MemberRow")) {
            const QString memberId = frame->property("memberId").toString();
            if (event->type() == QEvent::MouseButtonPress) {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                if (mouseEvent->button() == Qt::LeftButton) emit memberSelected(memberId);
            } else if (event->type() == QEvent::MouseButtonDblClick) {
                emit memberSelected(memberId);
                emit memberFocusRequested(memberId);
                return true;
            }
        } else if (frame->objectName() == QStringLiteral("SourceCard")) {
            if (event->type() == QEvent::MouseButtonPress) {
                auto *mouseEvent = static_cast<QMouseEvent *>(event);
                if (mouseEvent->button() == Qt::LeftButton) {
                    emit sourceRequested(frame->property("sourceId").toString());
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
void RoomPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_prototypePill) m_prototypePill->setVisible(width() >= 1380);
    if (m_directPill) m_directPill->setVisible(width() >= 1220);
    if (m_videoCountPill) m_videoCountPill->setVisible(width() >= 1160);
    if (m_controlHint) m_controlHint->setVisible(width() >= 1240);
    positionOverlays();
}
