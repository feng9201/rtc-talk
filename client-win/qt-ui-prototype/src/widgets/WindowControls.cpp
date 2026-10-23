#include "widgets/WindowControls.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QWindow>
#include <QVariant>

WindowControls::WindowControls(QWidget *parent)
    : QWidget(parent)
{
    setProperty("noWindowDrag", true);
    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    const auto makeButton = [this, layout](const QString &text, const QString &kind) {
        auto *button = new QPushButton(text, this);
        button->setProperty("kind", kind);
        button->setFixedSize(34, 28);
        button->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(button);
        return button;
    };

    auto *minimize = makeButton(QStringLiteral("−"), QStringLiteral("window"));
    auto *maximize = makeButton(QStringLiteral("□"), QStringLiteral("window"));
    auto *close = makeButton(QStringLiteral("×"), QStringLiteral("windowClose"));
    minimize->setToolTip(QStringLiteral("最小化"));
    maximize->setToolTip(QStringLiteral("最大化 / 还原"));
    close->setToolTip(QStringLiteral("关闭"));
    connect(minimize, &QPushButton::clicked, this, [this] {
        if (window()) window()->showMinimized();
    });
    connect(maximize, &QPushButton::clicked, this, [this] {
        if (!window()) return;
        if (window()->isMaximized()) window()->showNormal();
        else window()->showMaximized();
    });
    connect(close, &QPushButton::clicked, this, [this] {
        if (window()) window()->close();
    });
    setFixedSize(sizeHint());
}

QSize WindowControls::sizeHint() const
{
    return QSize(106, 28);
}