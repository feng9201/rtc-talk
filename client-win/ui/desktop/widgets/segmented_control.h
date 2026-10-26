#pragma once

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QWidget>

namespace talk::desktop {

class SegmentedControl : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentedControl(bool compact = false, QWidget *parent = nullptr);
    void addItem(const QString &id, const QString &text);
    void setCurrentId(const QString &id);
    QString currentId() const;
    void setItemEnabled(const QString &id, bool enabled);

signals:
    void currentIdChanged(const QString &id);

private:
    QButtonGroup *m_group;
    QHBoxLayout *m_layout;
    bool m_compact;
};

}  // namespace talk::desktop
