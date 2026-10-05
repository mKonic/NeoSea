#pragma once
// Items left to right, wrapping onto the next row: a shelf of books.

#include <QLayout>
#include <QList>

namespace neosea {

class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent = nullptr, int hSpacing = 22, int vSpacing = 22);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    void insertWidget(int index, QWidget *w);
    int count() const override { return int(m_items.size()); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override;
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return doLayout(QRect(0, 0, width, 0), true); }
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override;
    // the index an item dropped at this point would take
    int indexAt(const QPoint &p) const;

private:
    int doLayout(const QRect &rect, bool testOnly) const;
    QList<QLayoutItem *> m_items;
    int m_h, m_v;
};

} // namespace neosea
