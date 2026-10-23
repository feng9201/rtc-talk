#include "pages/PrejoinPage.h"

#include "base/Theme.h"
#include "widgets/Pill.h"
#include "video/VideoTile.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStyle>
#include <QScrollArea>
#include <QSlider>
#include <QVBoxLayout>

namespace {
QLabel *smallLabel(const QString &text, const QString &objectName = QStringLiteral("Hint"))
{
    return Theme::label(text, objectName);
}

QComboBox *makeCombo(const QVector<QPair<QString, QString>> &items, QWidget *parent = nullptr)
{
    auto *combo = new QComboBox(parent);
    for (const auto &item : items) combo->addItem(item.second, item.first);
    return combo;
}

QLabel *summaryValue(QWidget *parent)
{
    auto *label = Theme::label(QString(), QStringLiteral("SummaryValue"), parent);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return label;
}
} // namespace

PrejoinPage::PrejoinPage(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("PrejoinPage"));
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildHeader());

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("PrejoinContent"));
    auto *contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(26, 22, 26, 26);
    contentLayout->setSpacing(22);
    contentLayout->addWidget(buildFormCard(), 64);
    contentLayout->addWidget(buildPreviewColumn(), 36);
    scroll->setWidget(content);
    root->addWidget(scroll, 1);

    refreshSummary();
}

QWidget *PrejoinPage::buildHeader()
{
    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("AppHeader"));
    header->setFixedHeight(66);
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(22, 0, 126, 0);
    layout->setSpacing(12);

    auto *brand = new QLabel(QStringLiteral("RT"), header);
    brand->setObjectName(QStringLiteral("BrandMark"));
    brand->setAlignment(Qt::AlignCenter);
    brand->setFixedSize(42, 42);
    layout->addWidget(brand);

    auto *titles = new QVBoxLayout;
    titles->setContentsMargins(0, 0, 0, 0);
    titles->setSpacing(1);
    titles->addWidget(Theme::label(QStringLiteral("RTC Talk"), QStringLiteral("AppName"), header));
    titles->addWidget(Theme::label(QStringLiteral("视频对讲 · Windows / RK3576"), QStringLiteral("AppSubtitle"), header));
    layout->addLayout(titles);
    layout->addStretch(1);

    auto *step1 = new Pill(QStringLiteral("1 连接设置"), QStringLiteral("stepActive"), header);
    step1->setIcon(Theme::Icon::Settings);
    step1->setFixedHeight(32);
    auto *step2 = new Pill(QStringLiteral("2 房间"), QStringLiteral("step"), header);
    step2->setFixedHeight(32);
    layout->addWidget(step1);
    layout->addWidget(step2);
    return header;
}

QWidget *PrejoinPage::makeField(const QString &label, QWidget *editor, const QString &hint)
{
    auto *wrapper = new QWidget(this);
    auto *layout = new QVBoxLayout(wrapper);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    if (!label.isEmpty()) layout->addWidget(Theme::label(label, QStringLiteral("FormLabel"), wrapper));
    editor->setParent(wrapper);
    layout->addWidget(editor);
    if (!hint.isEmpty()) layout->addWidget(Theme::label(hint, QStringLiteral("FieldHint"), wrapper));
    return wrapper;
}

QWidget *PrejoinPage::makePair(QWidget *left, QWidget *right)
{
    auto *wrapper = new QWidget(this);
    auto *layout = new QHBoxLayout(wrapper);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    layout->addWidget(left, 1);
    layout->addWidget(right, 1);
    return wrapper;
}

QWidget *PrejoinPage::buildSourceOption(const QString &id, const QString &title, const QString &description, Theme::Icon icon)
{
    auto *button = new QPushButton(this);
    button->setProperty("kind", QStringLiteral("sourceOption"));
    button->setCheckable(true);
    button->setFixedHeight(68);
    button->setText(QStringLiteral("%1\n%2").arg(title, description));
    button->setProperty("sourceId", id);
    button->setIcon(QIcon(Theme::pixmap(icon, 22, QColor(QStringLiteral("#77cff5")))));
    button->setIconSize(QSize(24, 24));
    connect(button, &QPushButton::clicked, this, [this, id] { applySource(id); });
    return button;
}

QWidget *PrejoinPage::buildFormCard()
{
    auto *card = Theme::card(this);
    card->setMinimumWidth(600);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(22, 20, 22, 20);
    layout->setSpacing(14);

    auto *titleRow = new QHBoxLayout;
    auto *titles = new QVBoxLayout;
    titles->setSpacing(4);
    titles->addWidget(Theme::label(QStringLiteral("连接与入房"), QStringLiteral("Title"), card));
    titles->addWidget(Theme::label(QStringLiteral("先连接信令，再加入房间。视频默认关闭，入房后可按需开启。"),
                                   QStringLiteral("Subtitle"), card));
    titleRow->addLayout(titles, 1);
    auto *version = new Pill(QStringLiteral("原型 v2"), QStringLiteral("info"), card);
    version->setIcon(Theme::Icon::Info);
    titleRow->addWidget(version, 0, Qt::AlignTop);
    layout->addLayout(titleRow);

    m_server = new QLineEdit(QStringLiteral("ws://test.example.com:8080"), card);
    m_server->setClearButtonEnabled(true);
    layout->addWidget(makeField(QStringLiteral("信令服务"), m_server));

    m_userId = new QLineEdit(QStringLiteral("u1001"), card);
    m_roomId = new QLineEdit(QStringLiteral("review-room"), card);
    layout->addWidget(makePair(makeField(QStringLiteral("用户标识"), m_userId),
                               makeField(QStringLiteral("房间 ID"), m_roomId)));

    m_token = new QLineEdit(card);
    m_token->setEchoMode(QLineEdit::Password);
    m_token->setPlaceholderText(QStringLiteral("仅保存在内存，不写配置"));
    layout->addWidget(makeField(QStringLiteral("Token"), m_token));

    m_devToken = new QCheckBox(QStringLiteral("开发测试：从 /dev/token 获取凭证"), card);
    layout->addWidget(m_devToken);

    m_audioBackend = makeCombo({{QStringLiteral("wasapi"), QStringLiteral("WASAPI (Core Audio)")},
                                {QStringLiteral("sdl"), QStringLiteral("SDL3")},
                                {QStringLiteral("null"), QStringLiteral("Null / 自动测试")}}, card);
    m_mode = makeCombo({{QStringLiteral("auto"), QStringLiteral("自动（按人数选择）")},
                        {QStringLiteral("sfu"), QStringLiteral("SFU 多人媒体")},
                        {QStringLiteral("p2p"), QStringLiteral("P2P 直连")}}, card);
    layout->addWidget(makePair(makeField(QStringLiteral("音频后端"), m_audioBackend),
                               makeField(QStringLiteral("媒体模式"), m_mode)));

    m_talkMode = makeCombo({{QStringLiteral("free"), QStringLiteral("自由通话（全双工）")},
                            {QStringLiteral("ptt"), QStringLiteral("抢麦 PTT")},
                            {QStringLiteral("moderated"), QStringLiteral("主持人管控")}}, card);
    m_scenario = makeCombo({{QStringLiteral("multi"), QStringLiteral("多人 SFU")},
                            {QStringLiteral("one"), QStringLiteral("1v1 P2P")},
                            {QStringLiteral("sharing"), QStringLiteral("屏幕共享")},
                            {QStringLiteral("devices"), QStringLiteral("设备面板")},
                            {QStringLiteral("diagnostics"), QStringLiteral("诊断面板")}}, card);
    layout->addWidget(makePair(makeField(QStringLiteral("房间模式"), m_talkMode),
                               makeField(QStringLiteral("演示场景"), m_scenario)));

    layout->addWidget(Theme::separator(card));
    layout->addWidget(Theme::label(QStringLiteral("音频"), QStringLiteral("SectionTitle"), card));

    auto *sourceRow = new QHBoxLayout;
    sourceRow->setSpacing(12);
    m_micSource = qobject_cast<QPushButton *>(buildSourceOption(
        QStringLiteral("mic"), QStringLiteral("麦克风采集"), QStringLiteral("使用所选麦克风设备"), Theme::Icon::Mic));
    m_systemSource = qobject_cast<QPushButton *>(buildSourceOption(
        QStringLiteral("system"), QStringLiteral("系统音采集"), QStringLiteral("WASAPI loopback · 默认播放设备"), Theme::Icon::Headphones));
    auto *sourceGroup = new QButtonGroup(this);
    sourceGroup->setExclusive(true);
    sourceGroup->addButton(m_micSource);
    sourceGroup->addButton(m_systemSource);
    m_micSource->setChecked(true);
    sourceRow->addWidget(m_micSource, 1);
    sourceRow->addWidget(m_systemSource, 1);
    layout->addLayout(sourceRow);

    m_micDevice = makeCombo({{QStringLiteral("default"), QStringLiteral("系统默认（通信设备）")},
                              {QStringLiteral("mic-usb"), QStringLiteral("USB Audio Device")},
                              {QStringLiteral("mic-array"), QStringLiteral("Microphone Array")}}, card);
    m_playoutDevice = makeCombo({{QStringLiteral("default"), QStringLiteral("系统默认（通信设备）")},
                                  {QStringLiteral("speaker-usb"), QStringLiteral("USB Speakerphone")},
                                  {QStringLiteral("headset"), QStringLiteral("Bluetooth Headset")}}, card);
    layout->addWidget(makePair(makeField(QStringLiteral("麦克风设备"), m_micDevice),
                               makeField(QStringLiteral("播放设备"), m_playoutDevice)));

    auto *volumeRow = new QWidget(card);
    auto *volumeLayout = new QHBoxLayout(volumeRow);
    volumeLayout->setContentsMargins(0, 0, 0, 0);
    volumeLayout->setSpacing(10);
    m_volume = new QSlider(Qt::Horizontal, volumeRow);
    m_volume->setRange(0, 100);
    m_volume->setValue(60);
    m_volumeValue = Theme::label(QStringLiteral("60%"), QStringLiteral("ValueLabel"), volumeRow);
    m_volumeValue->setFixedWidth(42);
    m_volumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    volumeLayout->addWidget(m_volume, 1);
    volumeLayout->addWidget(m_volumeValue);
    layout->addWidget(makeField(QStringLiteral("播放音量"), volumeRow));

    layout->addWidget(Theme::separator(card));
    layout->addWidget(Theme::label(QStringLiteral("视频"), QStringLiteral("SectionTitle"), card));
    m_cameraDevice = makeCombo({{QStringLiteral("default"), QStringLiteral("系统默认摄像头")},
                                 {QStringLiteral("camera-usb"), QStringLiteral("USB Camera")},
                                 {QStringLiteral("camera-integrated"), QStringLiteral("Integrated Camera")}}, card);
    layout->addWidget(makeField(QStringLiteral("摄像头设备"), m_cameraDevice,
                                QStringLiteral("视频默认关闭；入房后可在底部控制栏或成员面板开启。")));

    layout->addStretch(1);
    layout->addWidget(Theme::separator(card));
    auto *actions = new QHBoxLayout;
    actions->setSpacing(10);
    actions->addStretch(1);
    auto *save = Theme::button(QStringLiteral("仅保存设置"), QStringLiteral("default"), card);
    auto *join = Theme::button(QStringLiteral("连接并加入房间"), QStringLiteral("primary"), card);
    join->setMinimumWidth(166);
    actions->addWidget(save);
    actions->addWidget(join);
    layout->addLayout(actions);

    connect(m_volume, &QSlider::valueChanged, this, [this](int value) {
        m_volumeValue->setText(QStringLiteral("%1%").arg(value));
    });
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshSummary(); });
    connect(m_talkMode, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshSummary(); });
    connect(m_scenario, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { refreshSummary(); });
    connect(save, &QPushButton::clicked, this, [this] { emit saveSettingsRequested(options()); });
    connect(join, &QPushButton::clicked, this, [this] { emit joinRequested(options()); });
    return card;
}

QWidget *PrejoinPage::buildSummaryCard()
{
    auto *card = Theme::card(this);
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(12);
    layout->addWidget(Theme::label(QStringLiteral("本次入房"), QStringLiteral("SectionTitle"), card));

    const auto addRow = [card, layout](const QString &name, QLabel **value) {
        auto *row = new QHBoxLayout;
        row->setSpacing(12);
        row->addWidget(Theme::label(name, QStringLiteral("Muted"), card));
        auto *v = summaryValue(card);
        row->addWidget(v, 1);
        layout->addLayout(row);
        *value = v;
        auto *line = Theme::separator(card);
        layout->addWidget(line);
    };

    addRow(QStringLiteral("音频"), &m_summaryAudio);
    addRow(QStringLiteral("视频"), &m_summaryVideo);
    addRow(QStringLiteral("媒体模式"), &m_summaryMode);
    addRow(QStringLiteral("房间模式"), &m_summaryTalk);
    addRow(QStringLiteral("演示场景"), &m_summaryScenario);

    auto *limitRow = new QHBoxLayout;
    limitRow->addWidget(Theme::label(QStringLiteral("规模上限"), QStringLiteral("Muted"), card));
    limitRow->addStretch(1);
    limitRow->addWidget(Theme::label(QStringLiteral("20 人 / 9 路视频"), QStringLiteral("SummaryValue"), card));
    layout->addLayout(limitRow);
    layout->addWidget(Theme::label(
        QStringLiteral("测试机带宽约 5 Mbps：视频默认关闭，720p 建议 1–2 路；多人优先 360p。"),
        QStringLiteral("Hint"), card));
    return card;
}

QWidget *PrejoinPage::buildPreviewColumn()
{
    auto *column = new QWidget(this);
    column->setMinimumWidth(360);
    auto *layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);

    auto *previewCard = Theme::card(column);
    auto *previewLayout = new QVBoxLayout(previewCard);
    previewLayout->setContentsMargins(14, 14, 14, 14);
    previewLayout->setSpacing(12);

    m_previewTile = new VideoTile(previewCard);
    m_previewTile->setMinimumHeight(310);
    MemberInfo self;
    self.id = QStringLiteral("me");
    self.name = QStringLiteral("我");
    self.role = QStringLiteral("host");
    self.isSelf = true;
    self.mic = false;
    self.camera = false;
    m_previewTile->setMember(self, true, false, false);
    previewLayout->addWidget(m_previewTile, 1);

    auto *identityRow = new QHBoxLayout;
    auto *identity = new QVBoxLayout;
    identity->setSpacing(2);
    identity->addWidget(Theme::label(QStringLiteral("我"), QStringLiteral("TitleSmall"), previewCard));
    identity->addWidget(Theme::label(QStringLiteral("u1001 · 主持人"), QStringLiteral("Hint"), previewCard));
    identityRow->addLayout(identity, 1);
    m_previewButton = Theme::button(QStringLiteral("预览摄像头"), QStringLiteral("default"), previewCard);
    m_previewButton->setMinimumWidth(106);
    identityRow->addWidget(m_previewButton);
    previewLayout->addLayout(identityRow);
    layout->addWidget(previewCard, 1);

    layout->addWidget(buildSummaryCard());
    connect(m_previewButton, &QPushButton::clicked, this, [this] {
        setPreviewEnabled(!m_previewEnabled);
        emit cameraPreviewToggled(m_previewEnabled);
    });
    return column;
}

QString PrejoinPage::comboData(const QComboBox *combo, const QString &fallback) const
{
    return combo && combo->currentIndex() >= 0 ? combo->currentData().toString() : fallback;
}

Scenario PrejoinPage::selectedScenario() const
{
    return scenarioFromString(comboData(m_scenario, QStringLiteral("multi")));
}

JoinOptions PrejoinPage::options() const
{
    JoinOptions options;
    options.server = m_server->text().trimmed();
    options.userId = m_userId->text().trimmed();
    options.roomId = m_roomId->text().trimmed();
    options.token = m_token->text();
    options.fetchDevToken = m_devToken->isChecked();
    options.audioBackend = comboData(m_audioBackend, QStringLiteral("wasapi"));
    options.modePref = comboData(m_mode, QStringLiteral("auto"));
    options.talkMode = comboData(m_talkMode, QStringLiteral("free"));
    options.scenario = selectedScenario();
    options.source = m_source;
    options.volume = m_volume->value();
    options.micDevice = comboData(m_micDevice, QStringLiteral("default"));
    options.playoutDevice = comboData(m_playoutDevice, QStringLiteral("default"));
    options.cameraDevice = comboData(m_cameraDevice, QStringLiteral("default"));
    return options;
}

void PrejoinPage::setScenario(Scenario scenario)
{
    const QString value = scenario == Scenario::One ? QStringLiteral("one")
        : scenario == Scenario::Sharing ? QStringLiteral("sharing")
        : scenario == Scenario::Devices ? QStringLiteral("devices")
        : scenario == Scenario::Diagnostics ? QStringLiteral("diagnostics")
        : QStringLiteral("multi");
    const int index = m_scenario->findData(value);
    if (index >= 0) m_scenario->setCurrentIndex(index);
}

void PrejoinPage::setPreviewEnabled(bool enabled)
{
    m_previewEnabled = enabled;
    MemberInfo self;
    self.id = QStringLiteral("me");
    self.name = QStringLiteral("我");
    self.role = QStringLiteral("host");
    self.isSelf = true;
    self.camera = enabled;
    m_previewTile->setMember(self, true, false, false);
    m_previewButton->setText(enabled ? QStringLiteral("关闭预览") : QStringLiteral("预览摄像头"));
}

void PrejoinPage::applySource(const QString &source)
{
    m_source = source;
    m_micSource->setChecked(source == QStringLiteral("mic"));
    m_systemSource->setChecked(source == QStringLiteral("system"));
    m_micSource->setProperty("selected", source == QStringLiteral("mic"));
    m_systemSource->setProperty("selected", source == QStringLiteral("system"));
    m_micSource->style()->unpolish(m_micSource);
    m_micSource->style()->polish(m_micSource);
    m_systemSource->style()->unpolish(m_systemSource);
    m_systemSource->style()->polish(m_systemSource);
    refreshSummary();
}

void PrejoinPage::refreshSummary()
{
    if (!m_summaryAudio) return;
    m_summaryAudio->setText(m_source == QStringLiteral("system")
        ? QStringLiteral("系统音 · 48 kHz · 32 kbps")
        : QStringLiteral("麦克风 · Opus 48 kHz · 32 kbps"));
    m_summaryVideo->setText(QStringLiteral("H.264 · 默认关闭"));
    m_summaryMode->setText(m_mode->currentText());
    m_summaryTalk->setText(m_talkMode->currentText());
    m_summaryScenario->setText(m_scenario->currentText());
}