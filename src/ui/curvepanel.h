#ifndef XN_CURVEPANEL_H
#define XN_CURVEPANEL_H

/*!
 * \file curvepanel.h
 * \brief 曲线显隐控制面板：分组复选框（前置颜色块）+ 已勾选名称标签栏 + 常用对比预设。
 */

#include "protocol.h"

#include <QVector>
#include <QWidget>

class FlowLayout;
class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QHBoxLayout;
class QLabel;
class QResizeEvent;
class QScrollArea;
class QVBoxLayout;
class QWidget;

namespace xn {

/*! \brief 曲线控制面板 */
class CurvePanel : public QWidget {
    Q_OBJECT
public:
    explicit CurvePanel(QWidget *parent = nullptr);

    /*! \brief 依据协议配置重建复选框 */
    void build(const FrameConfig &cfg);

    /*!
     * \brief 面板内容在给定宽度下“刚好完整显示”所需的高度。
     *
     * 主窗口据此分配绘图区/控制区的高度：分组框会随宽度换行，宽度越窄所需高度越大，
     * 写死高度必然出现某一侧被裁切（截图中的输入框折叠、分组框被切掉半行）。
     */
    int preferredHeight(int width) const;

    /*! \brief 设置勾选状态（不触发 curveToggled 信号） */
    void setCheckedSilently(int fieldIndex, bool checked);

    /*! \brief 批量更新开关：批处理期间不刷新“已勾选数据”标签，结束后统一刷新一次 */
    void beginBatchUpdate() { m_batchUpdating = true; }
    void endBatchUpdate();

    /*! \brief 设置全部勾选状态（不触发信号） */
    void setAllCheckedSilently(bool checked);

    bool isChecked(int fieldIndex) const;

    /*! \brief 回填某字段的手动偏差（不触发 fieldBiasChanged 信号） */
    void setBiasSilently(int fieldIndex, double value);

signals:
    /*! \brief 用户切换了某条曲线的显隐 */
    void curveToggled(int fieldIndex, bool checked);
    /*! \brief 用户修改了某偏差行的手动偏差（一次携带该行全部字段，统一刷新） */
    void fieldBiasChanged(const QVector<int> &fieldIndices, double value);
    /*! \brief 请求应用预设（取值同 PlotWidget::Preset） */
    void presetRequested(int preset);

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    /*!
     * \brief 创建一个分组的复选框区（数据列 + 行末共用偏差列）
     * \param title 分组标题
     * \param rows  每个元素是一行里“共用一个偏差框”的字段 key
     */
    QGroupBox *createGroupBox(const QString &title, const QVector<QVector<QString>> &rows,
                              QWidget *parent);

    void refreshCheckedLabel();

    /*! \brief 按当前文字行数调整“已勾选数据”栏高度：1~2 行自适应，不裁切文字 */
    void updateLegendHeight();

    FrameConfig m_cfg;
    /*! \brief 复选框表：**按字段序号索引**（界面上的分组排列顺序与字段序号不同，
     *          所有对外接口与内部读写都必须使用字段序号，切勿用控件创建顺序） */
    QVector<QCheckBox *> m_boxes;
    /*! \brief 偏差输入框表：**按字段序号索引**，同一偏差行的字段指向同一个输入框 */
    QVector<QDoubleSpinBox *> m_biasEdits;
    QVBoxLayout *m_rootLayout = nullptr;
    QHBoxLayout *m_presetLayout = nullptr; //!< 常用对比预设行（用于估算面板所需高度）
    QScrollArea *m_legendScroll = nullptr; //!< “已勾选数据”栏（高度随行数自适应）
    FlowLayout *m_legendFlow = nullptr;    //!< “已勾选数据”栏内的标签流式布局
    QVector<QLabel *> m_legendItems;       //!< 每个字段一个“色块 + 名称”标签（按字段序号）
    QLabel *m_legendPlaceholder = nullptr; //!< 未勾选任何数据时的占位文字
    QScrollArea *m_scroll = nullptr; //!< 包裹分组复选框，高度不足时垂直滚动，保证内容不被遮挡
    FlowLayout *m_groupsFlow = nullptr; //!< 分组框的自动换行布局（决定面板所需高度）
    QWidget *m_content = nullptr;
    bool m_batchUpdating = false; //!< 批量更新中：暂停标签刷新
};

} // namespace xn

#endif // XN_CURVEPANEL_H
