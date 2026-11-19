#ifndef FLOWLAYOUT_H
#define FLOWLAYOUT_H

/*!
 * \file flowlayout.h
 * \brief 自动换行（Flow）布局：宽度足够时横向排列，不足时自动换行。
 */

#include <QLayout>
#include <QList>
#include <QRect>
#include <QStyle>

/*!
 * \brief 自动换行布局（在 Qt 官方 FlowLayout 示例基础上补齐“适配”能力）
 *
 * 官方示例只按 sizeHint 摆放控件，宽度有余时行尾必然留出大片空白（界面表现为
 * “分布不均”），行内控件高度也各不相同（分组框高低参差）。本实现补充：
 *  - **整行铺满**（justify）：每行剩余宽度按各控件自然宽度等比例分摊，
 *    行尾不再残留空白，且行内各控件的相对大小保持不变；
 *  - **行内等高**（uniformHeight）：同一行内控件拉伸到相同高度，
 *    配合分组框内部底部的占位行，四/五个分组框的上下边框对齐；
 *  - sizeHint() 返回“所有控件排成一行”的自然尺寸，便于滚动区与父布局估算。
 */
class FlowLayout : public QLayout {
public:
    explicit FlowLayout(QWidget *parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = -1, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    // ── QLayout 必须实现的接口 ──
    void addItem(QLayoutItem *item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int) const override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QLayoutItem *takeAt(int index) override;

    /*! \brief 是否把每行剩余宽度按控件宽度比例分摊（默认开） */
    void setJustify(bool on);
    bool isJustify() const { return m_justify; }

    /*! \brief 是否让同一行内的控件等高（默认开） */
    void setUniformHeight(bool on);
    bool isUniformHeight() const { return m_uniformHeight; }

private:
    int doLayout(const QRect &rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem *> m_itemList;
    int m_hSpace;
    int m_vSpace;
    bool m_justify = true;       //!< 行内按比例分摊剩余宽度
    bool m_uniformHeight = true; //!< 行内控件等高
};

#endif // FLOWLAYOUT_H
