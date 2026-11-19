#ifndef XN_PLOTWIDGET_H
#define XN_PLOTWIDGET_H

/*!
 * \file plotwidget.h
 * \brief 绘图控件：25 条曲线叠加显示、框选放大、滚轮缩放、右键平移、悬停数值提示。
 */

#include "appsettings.h"
#include "framedata.h"
#include "protocol.h"

#include "qcustomplot.h"

#include <QPoint>
#include <QVector>

class QLabel;
class QRubberBand;
class QTimer;

namespace xn {

/*! \brief 曲线叠加绘图控件 */
class PlotWidget : public QCustomPlot {
    Q_OBJECT
public:
    /*! \brief 常用对比预设 */
    enum Preset {
        PresetAngle = 0, //!< 角度对比
        PresetRate,      //!< 速度对比
        PresetGyro,      //!< 惯导对比
        PresetAll,       //!< 全量显示
        PresetNone       //!< 清空全部
    };
    Q_ENUM(Preset)

    explicit PlotWidget(QWidget *parent = nullptr);

    /*! \brief 绑定数据源（数据生命周期由调用方保证）；visibility 非空时沿用其曲线显隐状态 */
    void setSource(const DataSet *data, const FrameConfig *cfg,
                   const QVector<bool> &visibility = QVector<bool>());

    /*! \brief 解除数据绑定 */
    void clearSource();

    /*! \brief 依据当前数据刷新全部曲线（按可视窗口抽取，见 refreshGraphs 实现） */
    void refreshGraphs();

    /*! \brief 应用显示配置（线宽、网格、抗锯齿、抽稀） */
    void setDisplayConfig(const DisplayConfig &cfg);
    DisplayConfig displayConfig() const { return m_display; }

    int curveCount() const { return m_graphs.size(); }
    bool hasData() const { return m_data && !m_data->isEmpty(); }

    void setCurveVisible(int fieldIndex, bool visible);
    bool isCurveVisible(int fieldIndex) const;
    QVector<bool> visibility() const { return m_visible; }
    void setAllVisible(bool visible);
    void applyPreset(Preset preset);

    /*! \brief 切换“修正后数值 / 未修正原始值”显示 */
    void setShowRawValues(bool showRaw);
    bool showRawValues() const { return m_showRaw; }
    bool hasCorrectedFields() const;

    /*! \brief 设置某字段的手动偏差（同一偏差行的字段取相同值） */
    void setFieldBias(int fieldIndex, double value);
    /*! \brief 切换“显示偏差计算”：开启后曲线值 = 基础值(原始/修正) + 手动偏差 */
    void setShowBias(bool show);
    bool showBias() const { return m_showBias; }

    /*! \brief 一键复位到全局视图 */
    void resetView();

    /*! \brief Y 轴自适应所有可见曲线（上下留 5% 余量） */
    void fitYToVisible();

    /*! \brief 导出当前视图为 PNG */
    bool exportPng(const QString &path, QString *error);

signals:
    /*! \brief 曲线显隐变化（用于同步复选框） */
    void visibilityChanged(int fieldIndex, bool visible);
    /*! \brief 鼠标在图表内移动：帧序号与 Y 轴数值 */
    void cursorMoved(int frameIndex, double value, bool valid);
    /*! \brief 视图范围发生变化 */
    void viewChanged();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildGraphs();
    void applyDisplayToGraphs();
    /*! \brief 仅更新单条曲线的显隐状态并发出信号；返回是否发生了变化（不做 Y 自适应/重绘） */
    bool applyCurveVisible(int fieldIndex, bool visible);
    void updateHover(const QPoint &pos);
    void hideHover();
    void layoutHoverLabel();
    void limitXRange();
    /*! \brief 限制 Y 轴平移范围，避免把曲线整体拖出视野 */
    void limitYRange();
    void panTo(const QPoint &pos);
    /*! \brief 视图变化后延迟重载曲线数据（去抖，避免拖动/滚轮期间频繁重取数据） */
    void scheduleReload();

    /*! \brief 取当前显示模式下的数值（修正后 / 未修正原始值） */
    double displayValue(int frame, int field) const;

    const DataSet *m_data = nullptr;
    const FrameConfig *m_cfg = nullptr;

    QVector<QCPGraph *> m_graphs;
    QVector<bool> m_visible;
    QVector<QCPItemTracer *> m_tracers;
    QCPItemLine *m_cursorLine = nullptr;
    QLabel *m_hoverLabel = nullptr;
    QRubberBand *m_rubberBand = nullptr;

    DisplayConfig m_display;
    bool m_showRaw = false;     //!< 是否显示未修正原始值
    bool m_showBias = false;    //!< 是否在基础值上叠加手动偏差
    QVector<double> m_bias;     //!< 每字段手动偏差（默认 0）
    /*! \brief 当前已装载到绘图控件的数据窗口与抽稀步长（用于判断是否需要重新装载） */
    int m_loadedStep = 0;
    QCPRange m_loadedKeyRange;
    bool m_loadedRaw = false;   //!< 已装载数据的数值模式（原始值/修正值）
    bool m_loadedBias = false;  //!< 已装载数据的偏差模式（叠加/不叠加）
    QCPRange m_dataYRange;      //!< 最近一次 Y 自适应得到的数据实际范围
    bool m_dataYRangeValid = false;
    QTimer *m_reloadTimer = nullptr;
    bool m_panning = false;
    bool m_banding = false;
    bool m_hovering = false;
    QPoint m_pressPos;
    QCPRange m_panXRange;
    QCPRange m_panYRange;
};

} // namespace xn

#endif // XN_PLOTWIDGET_H
