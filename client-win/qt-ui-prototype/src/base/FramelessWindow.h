#pragma once

#include <QWidget>

class FramelessWindow : public QWidget
{
    Q_OBJECT

public:
    explicit FramelessWindow(QWidget *parent = nullptr);

public slots:
    void toggleMaximize();

protected:
    bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
    void paintEvent(QPaintEvent *event) override;

private:
    bool isInteractiveAt(const QPoint &globalPos) const;
    qreal borderWidth() const;
    bool m_minBoxEnsured = false;
};