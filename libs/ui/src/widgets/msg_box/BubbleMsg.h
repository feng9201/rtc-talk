#pragma once

/*
 * BubbleMsg.h
 * @brief 气泡消息提示组件
 *
 * 功能:
 *   1. BubbleMsg          基础气泡，支持通知(绿)/警告(黄)/错误(红)，可定时自动隐藏
 *   2. BubbleMsgWithClose 带关闭按钮的气泡，可常驻直到用户点击关闭
 *   3. MulBubbleMsg       多气泡管理器，从父窗口顶部依次向下堆叠，间隔 20px
 */

#include <QWidget>
#include <QString>
#include <QColor>
#include <QList>
#include <QSet>
#include <atomic>
#include <mutex>

class QTimer;
class QLabel;
class QPushButton;
class QHBoxLayout;
class QGraphicsOpacityEffect;

// ---- 消息类型 ----
enum class BubbleType {
    Info = 0,   // 通知（绿色）
    Warning,            // 警告（黄色）
    Error               // 错误（红色）
};

// ---- 弹出位置（相对父窗口）----
enum class BubblePosition {
    Top = 0,    // 顶部居中
    Center,     // 垂直居中
    Bottom      // 底部（距底 100px）
};

// 默认自动隐藏时长（毫秒）
constexpr int kBubbleDefaultKeepTimeMs = 2500;

// ---- 基础气泡 ----
class BubbleMsg : public QWidget {
    Q_OBJECT

public:
    explicit BubbleMsg(QWidget* parent = nullptr);
    ~BubbleMsg() override;

    // 显示消息
    // keepTimeMs: 自动隐藏时长（毫秒），<= 0 表示不自动隐藏
    virtual void showMessage(BubbleType type, const QString& msg,
                             BubblePosition pos = BubblePosition::Top,
                             int keepTimeMs = kBubbleDefaultKeepTimeMs);

signals:
    // 气泡结束（自动隐藏或点击关闭后发出）
    void finished();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

    // 依据类型设置背景色 / 边框色 / 类型圆点色
    void applyTypeStyle(BubbleType type);

    // 依据父窗口与位置枚举计算并移动自身
    void reposition();

    // 设置整体透明度（0.0 ~ 1.0）
    void setBubbleOpacity(double opacity);

    // 关闭气泡：停止定时器、隐藏并发出 finished 信号
    void closeBubble();

    // 除文字外所需的额外宽度（子类放关闭按钮时重写）
    virtual int extraWidth() const { return 0; }

private slots:
    void onTimeout();

protected:
    BubbleType type_ = BubbleType::Info;
    BubblePosition pos_ = BubblePosition::Top;
    int keep_time_ms_ = kBubbleDefaultKeepTimeMs;
    int remain_ms_ = 0;

    QColor bg_color_;
    QColor border_color_;

    QTimer* timer_ = nullptr;
    QGraphicsOpacityEffect* opacity_effect_ = nullptr;
    QHBoxLayout* layout_ = nullptr;
    QLabel* icon_lb_ = nullptr;
    QLabel* content_lb_ = nullptr;
};

// ---- 带关闭按钮的气泡 ----
class BubbleMsgWithClose : public BubbleMsg {
    Q_OBJECT

public:
    explicit BubbleMsgWithClose(QWidget* parent = nullptr);

    // 显示消息：默认不自动隐藏（常驻），用户点击关闭按钮关闭
    void showMessage(BubbleType type, const QString& msg,
                     BubblePosition pos = BubblePosition::Top,
                     int keepTimeMs = 0) override;

protected:
    int extraWidth() const override { return 32; }

private slots:
    void onCloseClicked();

private:
    QPushButton* btn_close_ = nullptr;
};

// ---- 多气泡管理器 ----
class MulBubbleMsg : public QObject {
    Q_OBJECT

public:
    // parent: 气泡挂载的父窗口（所有气泡相对它定位）
    explicit MulBubbleMsg(QWidget* parent);
    ~MulBubbleMsg() override;

    // 弹出一条消息，默认 2.5s 自动隐藏
    void showMessage(BubbleType type, const QString& msg,
                     int keepTimeMs = kBubbleDefaultKeepTimeMs);

    // 清空所有正在显示的气泡
    void clearAll();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Entry {
        BubbleMsg* bubble = nullptr;
        int slot = 0;
    };

    // 分配最小的空闲槽位（0 为最顶部）
    int allocSlot();

    // 重新排列所有气泡
    void relayout();

private:
    QWidget* parent_ = nullptr;
    QList<Entry> entries_;
    QSet<int> used_slots_;
};

// ---- 多气泡管理器（单例） ----
class SigMulBubbleMsg : public MulBubbleMsg {
    Q_OBJECT

public:
    // 获取单例；首次调用需在 UI 线程（决定 QObject 线程归属）
    static SigMulBubbleMsg* Instance(QWidget* parent = nullptr);

    ~SigMulBubbleMsg() override = default;

private:
    // 单例：禁止外部直接构造
    explicit SigMulBubbleMsg(QWidget* parent);

    static std::atomic<SigMulBubbleMsg*> s_instance_;
    static std::mutex s_mutex_;
};
