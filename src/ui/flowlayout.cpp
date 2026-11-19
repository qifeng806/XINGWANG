#include "flowlayout.h"

#include <QWidget>

FlowLayout::FlowLayout(QWidget *parent, int margin, int hSpacing, int vSpacing)
    : QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::FlowLayout(int margin, int hSpacing, int vSpacing)
    : m_hSpace(hSpacing), m_vSpace(vSpacing)
{
    setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem *item = takeAt(0))
        delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
    m_itemList.append(item);
}

int FlowLayout::horizontalSpacing() const
{
    if (m_hSpace >= 0)
        return m_hSpace;
    return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
}

int FlowLayout::verticalSpacing() const
{
    if (m_vSpace >= 0)
        return m_vSpace;
    return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
}

void FlowLayout::setJustify(bool on)
{
    if (m_justify == on)
        return;
    m_justify = on;
    invalidate();
}

void FlowLayout::setUniformHeight(bool on)
{
    if (m_uniformHeight == on)
        return;
    m_uniformHeight = on;
    invalidate();
}

int FlowLayout::count() const
{
    return m_itemList.size();
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
    return m_itemList.value(index);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
    if (index >= 0 && index < m_itemList.size())
        return m_itemList.takeAt(index);
    return nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
    return {};
}

bool FlowLayout::hasHeightForWidth() const
{
    return true;
}

int FlowLayout::heightForWidth(int width) const
{
    return doLayout(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect &rect)
{
    QLayout::setGeometry(rect);
    doLayout(rect, false);
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem *item : m_itemList) {
        if (item->isEmpty())
            continue; // 隐藏的控件不参与计算
        size = size.expandedTo(item->minimumSize());
    }
    const QMargins m = contentsMargins();
    size += QSize(m.left() + m.right(), m.top() + m.bottom());
    return size;
}

QSize FlowLayout::sizeHint() const
{
    // “所有控件排成一行”的自然尺寸：滚动区与父布局据此估算内容大小
    int width = 0;
    int height = 0;
    int count = 0;
    const int hSpace = horizontalSpacing();
    for (const QLayoutItem *item : m_itemList) {
        if (item->isEmpty())
            continue;
        const QSize hint = item->sizeHint();
        if (count++ > 0)
            width += hSpace;
        width += hint.width();
        height = qMax(height, hint.height());
    }
    const QMargins m = contentsMargins();
    return QSize(width + m.left() + m.right(), height + m.top() + m.bottom());
}

/*!
 * \brief 按行摆放控件；testOnly 为真时只计算高度、不改变任何控件几何。
 *
 * 分行只依据控件的**自然宽度**（sizeHint 与最小宽度的较大者），因此 heightForWidth()
 * 与 setGeometry() 的分行结果完全一致；分行完成后再把行内剩余宽度按自然宽度比例
 * 分摊给行内控件，使每行铺满可用宽度。
 */
int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
    int left, top, right, bottom;
    getContentsMargins(&left, &top, &right, &bottom);
    const QRect effectiveRect = rect.adjusted(+left, +top, -right, -bottom);
    const int hSpace = horizontalSpacing();
    const int vSpace = verticalSpacing();
    const int available = qMax(0, effectiveRect.width());

    int y = effectiveRect.y();
    int totalHeight = 0;
    int lineHeight = 0;
    int lineWidth = 0; //!< 当前行自然宽度合计（含行内间距）
    QList<QLayoutItem *> line;

    // 结束当前行：分摊剩余宽度 → 逐项设置几何 → 推进 y
    const auto flushLine = [&]() {
        if (line.isEmpty())
            return;
        const int extra = available - lineWidth;
        int x = effectiveRect.x();
        int used = 0;
        for (int i = 0; i < line.size(); ++i) {
            QLayoutItem *item = line.at(i);
            QSize size = item->sizeHint();
            if (m_justify && line.size() > 1 && extra > 0 && lineWidth > 0) {
                // 最后一个控件吃掉整除余数，保证行尾严丝合缝
                const int add = (i == line.size() - 1)
                                    ? extra - used
                                    : static_cast<int>(static_cast<qint64>(extra) * size.width()
                                                       / lineWidth);
                size.setWidth(size.width() + add);
                used += add;
            }
            size.setWidth(qMax(size.width(), item->minimumSize().width()));
            // 极端窄窗口下不让控件越出可视宽度（除非其最小宽度本身就超出）
            const int minWidth = item->minimumSize().width();
            size.setWidth(qBound(minWidth, size.width(), qMax(available, minWidth)));
            if (m_uniformHeight && line.size() > 1)
                size.setHeight(qMax(size.height(), lineHeight));
            if (!testOnly)
                item->setGeometry(QRect(QPoint(x, y), size));
            x += size.width() + hSpace;
        }
        if (totalHeight > 0)
            totalHeight += vSpace;
        totalHeight += lineHeight;
        y += lineHeight + vSpace;
        line.clear();
        lineWidth = 0;
        lineHeight = 0;
    };

    for (QLayoutItem *item : m_itemList) {
        if (item->isEmpty())
            continue; // 隐藏的控件不占位（“已勾选数据”栏靠显隐标签增删条目）
        const QSize hint = item->sizeHint();
        const int itemWidth = qMax(hint.width(), item->minimumSize().width());
        if (!line.isEmpty() && lineWidth + hSpace + itemWidth > available)
            flushLine(); // 当前行放不下 → 换行
        if (!line.isEmpty())
            lineWidth += hSpace;
        lineWidth += itemWidth;
        lineHeight = qMax(lineHeight, hint.height());
        line.append(item);
    }
    flushLine();

    return top + totalHeight + bottom;
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
    QObject *parent = this->parent();
    if (!parent)
        return -1;
    if (parent->isWidgetType()) {
        auto *pw = static_cast<QWidget *>(parent);
        return pw->style()->pixelMetric(pm, nullptr, pw);
    }
    return static_cast<QLayout *>(parent)->spacing();
}
