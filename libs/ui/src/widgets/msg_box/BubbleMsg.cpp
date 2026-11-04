/*
 * BubbleMsg.cpp
 * @brief 气泡消息提示组件实现
 */

#include "BubbleMsg.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QGraphicsOpacityEffect>
#include <QIcon>
#include <QSize>
#include <QThread>
#include <QEvent>
#include <QVariant>

namespace {
// 气泡外观
constexpr int kBubbleHeight = 40;    // 气泡高度
constexpr int kBubbleMinWidth = 160; // 最小宽度
constexpr int kBubbleMaxWidth = 420; // 最大宽度
constexpr int kIconDotSize = 10;     // 类型圆点直径
constexpr int kTopMargin = 10;       // 距父窗口顶部
constexpr int kSlotGap = 20;         // 多气泡相邻间隔
constexpr int kFadeMs = 300;         // 淡出时长
constexpr int kTimerIntervalMs = 50; // 定时器周期
}

// ============ BubbleMsg ============

BubbleMsg::BubbleMsg(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setProperty("noWindowDrag", true);
    if (parent) parent->installEventFilter(this);

    layout_ = new QHBoxLayout(this);
    layout_->setContentsMargins(16, 8, 16, 8);
    layout_->setSpacing(8);

    icon_lb_ = new QLabel(this);
    icon_lb_->setFixedSize(kIconDotSize, kIconDotSize);

    content_lb_ = new QLabel(this);
    content_lb_->setTextFormat(Qt::PlainText);
    content_lb_->setWordWrap(true);
    content_lb_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    content_lb_->setStyleSheet(QStringLiteral("color:#262626; font: 13px 'Microsoft YaHei';"));

    layout_->addStretch();
    layout_->addWidget(icon_lb_);
    layout_->addWidget(content_lb_);
    layout_->addStretch();

    timer_ = new QTimer(this);
    timer_->setInterval(kTimerIntervalMs);
    connect(timer_, &QTimer::timeout, this, &BubbleMsg::onTimeout);
}

BubbleMsg::~BubbleMsg() = default;

void BubbleMsg::showMessage(BubbleType type, const QString& msg,
                            BubblePosition pos, int keepTimeMs)
{
    pos_ = pos;
    keep_time_ms_ = keepTimeMs;
    remain_ms_ = keep_time_ms_;
    setBubbleOpacity(1.0);

    applyTypeStyle(type);
    content_lb_->setText(msg);

    // 依据文字宽度动态调整气泡宽度
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPixelSize(13);
    QFontMetrics fm(font);
    int w = qBound(kBubbleMinWidth, fm.horizontalAdvance(msg) + 64 + extraWidth(), kBubbleMaxWidth);
    const int textWidth = w - 64 - extraWidth();
    const QRect textRect = fm.boundingRect(QRect(0, 0, textWidth, 0), Qt::TextWordWrap, msg);
    content_lb_->setFixedWidth(textWidth);
    setFixedSize(w, qMax(kBubbleHeight, textRect.height() + 16));

    reposition();

    timer_->stop();
    show();
    raise();

    if (keep_time_ms_ > 0) {
        timer_->start();
    }
}

bool BubbleMsg::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize &&
        !property("managedBubble").toBool()) reposition();
    return QWidget::eventFilter(watched, event);
}

void BubbleMsg::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QRectF rect = QRectF(this->rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(rect, 6, 6);

    painter.fillPath(path, bg_color_);
    painter.setPen(QPen(border_color_, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
}

void BubbleMsg::applyTypeStyle(BubbleType type)
{
    type_ = type;
    switch (type_) {
    case BubbleType::Info:
        bg_color_ = QColor("#E8F5E9");
        border_color_ = QColor("#52C41A");
        icon_lb_->setStyleSheet(QStringLiteral("background-color:#52C41A; border-radius:5px;"));
        break;
    case BubbleType::Warning:
        bg_color_ = QColor("#FFF8E1");
        border_color_ = QColor("#FAAD14");
        icon_lb_->setStyleSheet(QStringLiteral("background-color:#FAAD14; border-radius:5px;"));
        break;
    case BubbleType::Error:
        bg_color_ = QColor("#FDECEA");
        border_color_ = QColor("#FF4D4F");
        icon_lb_->setStyleSheet(QStringLiteral("background-color:#FF4D4F; border-radius:5px;"));
        break;
    }
}

void BubbleMsg::reposition()
{
    QWidget* p = parentWidget();
    if (!p) {
        return;
    }

    int x = (p->width() - width()) / 2;
    int y = kTopMargin;
    switch (pos_) {
    case BubblePosition::Top:
        y = kTopMargin;
        break;
    case BubblePosition::Center:
        y = (p->height() - height()) / 2;
        break;
    case BubblePosition::Bottom:
        y = p->height() - height() - 100;
        break;
    }
    move(x, y);
}

void BubbleMsg::setBubbleOpacity(double opacity)
{
    if (!opacity_effect_) {
        opacity_effect_ = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(opacity_effect_);
    }
    opacity_effect_->setOpacity(qBound(0.0, opacity, 1.0));
}

void BubbleMsg::closeBubble()
{
    timer_->stop();
    hide();
    emit finished();
}

void BubbleMsg::onTimeout()
{
    remain_ms_ -= kTimerIntervalMs;
    if (remain_ms_ <= 0) {
        closeBubble();
        return;
    }

    // 最后 kFadeMs 内淡出
    if (remain_ms_ < kFadeMs) {
        setBubbleOpacity(qreal(remain_ms_) / kFadeMs);
    }
}

// ============ BubbleMsgWithClose ============

BubbleMsgWithClose::BubbleMsgWithClose(QWidget* parent)
    : BubbleMsg(parent)
{
    btn_close_ = new QPushButton(this);
    btn_close_->setIcon(QIcon(":/res/common/close.png"));
    btn_close_->setIconSize(QSize(12, 12));
    btn_close_->setFixedSize(20, 20);
    btn_close_->setCursor(Qt::PointingHandCursor);
    btn_close_->setStyleSheet(QStringLiteral(
        "QPushButton{background:transparent;border:none;padding:0;"
        "min-width:20px;max-width:20px;min-height:20px;max-height:20px;}"
        "QPushButton:hover{background:rgba(0,0,0,0.08);border-radius:10px;}"));

    // 插入到内容与末尾弹簧之间
    layout_->insertWidget(layout_->count() - 1, btn_close_);

    connect(btn_close_, &QPushButton::clicked, this, &BubbleMsgWithClose::onCloseClicked);
}

void BubbleMsgWithClose::showMessage(BubbleType type, const QString& msg,
                                     BubblePosition pos, int keepTimeMs)
{
    // 复用基类逻辑；常驻时（keepTimeMs <= 0）基类不会启动定时器
    BubbleMsg::showMessage(type, msg, pos, keepTimeMs);
}

void BubbleMsgWithClose::onCloseClicked()
{
    closeBubble();
}

// ============ MulBubbleMsg ============

MulBubbleMsg::MulBubbleMsg(QWidget* parent)
    : QObject(parent)
    , parent_(parent)
{
    if (parent_) parent_->installEventFilter(this);
}

bool MulBubbleMsg::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == parent_ && event->type() == QEvent::Resize) relayout();
    return QObject::eventFilter(watched, event);
}

MulBubbleMsg::~MulBubbleMsg()
{
    clearAll();
}

void MulBubbleMsg::showMessage(BubbleType type, const QString& msg, int keepTimeMs)
{
    if (!parent_) {
        return;
    }

    // 可能从子线程调用：非 UI 线程时投递到 UI 线程执行
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(this, [this, type, msg, keepTimeMs]() {
            showMessage(type, msg, keepTimeMs);
        }, Qt::QueuedConnection);
        return;
    }

    auto* bubble = new BubbleMsg(parent_);
    bubble->setProperty("managedBubble", true);
    const int slot = allocSlot();

    connect(bubble, &BubbleMsg::finished, this, [this, bubble]() {
        // 释放槽位并重排剩余气泡
        for (int i = 0; i < entries_.size(); ++i) {
            if (entries_[i].bubble == bubble) {
                used_slots_.remove(entries_[i].slot);
                entries_.removeAt(i);
                break;
            }
        }
        bubble->deleteLater();
        relayout();
    });

    entries_.append(Entry{bubble, slot});
    bubble->showMessage(type, msg, BubblePosition::Top, keepTimeMs);
    relayout();
}

void MulBubbleMsg::clearAll()
{
    for (const auto& entry : entries_) {
        delete entry.bubble;
    }
    entries_.clear();
    used_slots_.clear();
}

int MulBubbleMsg::allocSlot()
{
    int slot = 0;
    while (used_slots_.contains(slot)) {
        ++slot;
    }
    used_slots_.insert(slot);
    return slot;
}

void MulBubbleMsg::relayout()
{
    if (!parent_) {
        return;
    }

    int y = kTopMargin;
    for (const auto& entry : entries_) {
        if (!entry.bubble) {
            continue;
        }
        const int x = (parent_->width() - entry.bubble->width()) / 2;
        entry.bubble->move(x, y);
        y += entry.bubble->height() + kSlotGap;
    }
}

// ============ SigMulBubbleMsg ============

std::atomic<SigMulBubbleMsg*> SigMulBubbleMsg::s_instance_;
std::mutex SigMulBubbleMsg::s_mutex_;

SigMulBubbleMsg* SigMulBubbleMsg::Instance(QWidget* parent)
{
    SigMulBubbleMsg* tmp = s_instance_.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);  // 获取内存 fence
    if (tmp == nullptr) {
        std::lock_guard<std::mutex> lock(s_mutex_);
        tmp = s_instance_.load(std::memory_order_relaxed);
        if (tmp == nullptr) {
            tmp = new SigMulBubbleMsg(parent);
            std::atomic_thread_fence(std::memory_order_release);  // 释放内存 fence
            s_instance_.store(tmp, std::memory_order_relaxed);
        }
    }
    return tmp;
}

SigMulBubbleMsg::SigMulBubbleMsg(QWidget* parent)
    : MulBubbleMsg(parent)
{
}

