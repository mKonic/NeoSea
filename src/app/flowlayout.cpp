#include "app/flowlayout.h"

#include <QWidget>
#include <QWidgetItem>

namespace neosea {

FlowLayout::FlowLayout(QWidget *parent, int hSpacing, int vSpacing) : QLayout(parent), m_h(hSpacing), m_v(vSpacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem *item = takeAt(0)) delete item;
}

void FlowLayout::addItem(QLayoutItem *item) { m_items.append(item); }

void FlowLayout::insertWidget(int index, QWidget *w)
{
    addChildWidget(w);
    m_items.insert(std::clamp<qsizetype>(index, 0, m_items.size()), new QWidgetItem(w));
    invalidate();
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    if (index < 0 || index >= m_items.size()) return nullptr;
    return m_items.takeAt(index);
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (QLayoutItem *item : m_items) size = size.expandedTo(item->minimumSize());
    const QMargins m = contentsMargins();
    return size + QSize(m.left() + m.right(), m.top() + m.bottom());
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
    const QMargins m = contentsMargins();
    const QRect r = rect.adjusted(m.left(), m.top(), -m.right(), -m.bottom());
    int x = r.x(), y = r.y(), lineHeight = 0;
    // rows sit on a common floor, as books on a shelf do
    QList<QPair<QLayoutItem *, QPoint>> row;
    auto flushRow = [&] {
        for (auto &[item, pt] : row) {
            const QSize s = item->sizeHint();
            if (!testOnly) item->setGeometry(QRect(QPoint(pt.x(), pt.y() + lineHeight - s.height()), s));
        }
        row.clear();
    };
    for (QLayoutItem *item : m_items) {
        if (item->widget() && item->widget()->isHidden()) continue;
        const QSize s = item->sizeHint();
        int nextX = x + s.width() + m_h;
        if (nextX - m_h > r.right() + 1 && lineHeight > 0) {
            flushRow();
            x = r.x();
            y += lineHeight + m_v;
            nextX = x + s.width() + m_h;
            lineHeight = 0;
        }
        row.append({item, QPoint(x, y)});
        x = nextX;
        lineHeight = std::max(lineHeight, s.height());
    }
    flushRow();
    return y + lineHeight - rect.y() + m.bottom();
}

int FlowLayout::indexAt(const QPoint &p) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        const QRect g = m_items[i]->geometry();
        // above this item's row, or on its row and left of its middle
        if (p.y() < g.top() || (p.y() < g.bottom() && p.x() < g.center().x())) return i;
    }
    return int(m_items.size());
}

} // namespace neosea
