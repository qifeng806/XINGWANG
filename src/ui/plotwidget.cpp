#include "plotwidget.h"

#include <QLabel>
#include <QMouseEvent>
#include <QRubberBand>
#include <QStringList>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace xn {

namespace {

/*! \brief 视图变化后重新装载曲线数据的去抖间隔（毫秒） */
constexpr int kReloadDebounceMs = 60;

/*!
 * \brief 帧序号刻度生成器：X 轴只用整数刻度，且步长取 1/2/5×10ⁿ 这类“整齐”值
 *
 * QCPAxisTicker 默认可能给出 2.5 这类含小数的步长（cleanMantissa 的候选值之一），
 * 对“帧序号”轴并不合适，这里统一向上取到整齐的整数步长（1、2、5、10、20、50…）。
 */
class FrameAxisTicker : public QCPAxisTicker {
protected:
    double getTickStep(const QCPRange &range) override
    {
        const double base = QCPAxisTicker::getTickStep(range);
        if (!std::isfinite(base) || base <= 1.0)
            return 1.0; // 退化范围（span<=0 等）会返回 NaN，必须兜底
        const double magnitude = std::pow(10.0, std::floor(std::log10(base)));
        for (double m : {1.0, 2.0, 5.0, 10.0}) {
            if (magnitude * m >= base - 1e-9)
                return magnitude * m;
        }
        return std::ceil(base);
    }
};

} // namespace

PlotWidget::PlotWidget(QWidget *parent)
    : QCustomPlot(parent)
{
    setInteractions(QCP::iRangeZoom);      // 滚轮缩放使用内置实现（以光标为中心）
    setNoAntialiasingOnDrag(true);         // 拖动/缩放过程中关闭抗锯齿，保证流畅
    setSelectionRectMode(QCP::srmNone);    // 框选放大自行实现（需要与右键平移并存）
    // 滚轮只缩放“帧序号”轴：Y 轴由自适应管理（见 wheelEvent）。
    // 若同时缩放 Y，X 被 limitXRange 夹住后 Y 会无限放大，导致曲线被压成平线且无法恢复。
    axisRect()->setRangeZoom(Qt::Horizontal);
    axisRect()->setRangeZoomFactor(0.85, 0.85); // 每格缩放 15%

    xAxis->setLabel(QStringLiteral("帧序号"));
    yAxis->setLabel(QStringLiteral("数值"));
    xAxis->setTicker(QSharedPointer<QCPAxisTicker>(new FrameAxisTicker));
    xAxis->grid()->setSubGridVisible(true);
    yAxis->grid()->setSubGridVisible(true);
    legend->setVisible(false);             // 用底部复选框代替图例

    setBackground(QBrush(QColor(0xFA, 0xFA, 0xFA)));

    m_cursorLine = new QCPItemLine(this);
    m_cursorLine->setPen(QPen(QColor(0x88, 0x88, 0x88), 1, Qt::DashLine));
    m_cursorLine->setClipToAxisRect(true);
    m_cursorLine->setClipAxisRect(axisRect());
    m_cursorLine->setVisible(false);

    m_hoverLabel = new QLabel(this);
    m_hoverLabel->setTextFormat(Qt::RichText);
    m_hoverLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
    // m_hoverLabel->setStyleSheet(QStringLiteral(
    //     "QLabel { background-color: rgba(255,255,255,235); border: 1px solid #9E9E9E;"
    //     " border-radius: 3px; padding: 4px 6px; }"));

    m_hoverLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #202020; background-color: rgba(255,255,255,235);"
        " border: 1px solid #9E9E9E; border-radius: 3px; padding: 4px 6px; }"));

    m_hoverLabel->setFont(QFont(QStringLiteral("Microsoft YaHei"), 8));
    m_hoverLabel->hide();

    m_rubberBand = new QRubberBand(QRubberBand::Rectangle, this);
    m_rubberBand->hide();

    // 视图变化后延迟重载曲线数据（去抖）
    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(kReloadDebounceMs);
    connect(m_reloadTimer, &QTimer::timeout, this, [this]() { refreshGraphs(); });

    m_display = AppSettings::loadDisplayConfig();
    applyDisplayToGraphs();
}

void PlotWidget::setSource(const DataSet *dataSet, const FrameConfig *cfg,
                           const QVector<bool> &visibility)
{
    m_data = dataSet;
    m_cfg = cfg;
    m_showRaw = m_display.showRawValues && hasCorrectedFields(); // 恢复上次的数值显示模式
    // 装载阶段先按基础值：buildGraphs 会重置 m_bias，若此处保持偏差模式会先用“全0偏差”
    // 装载一遍；统一改为 false，等调用方恢复 m_bias 后再 setShowBias 切回。
    m_showBias = false;
    m_loadedStep = 0;                                            // 换数据源必须整窗重载
    m_dataYRangeValid = false;
    buildGraphs();

    if (!hasData()) {
        xAxis->setRange(0, 1);
        yAxis->setRange(0, 1);
        replot();
        return;
    }

    // 先复位 X 轴到全局视图，再装载数据：否则第一次装载会沿用上一个数据集的 X 范围，
    // 随后 resetView 又要重新装载一遍（大文件下等于双倍装载开销）。
    xAxis->setRange(-0.5, double(m_data->frameCount() - 1) + 0.5);

    // 重新解析/换文件后保留用户的曲线勾选状态（仅当字段数一致时；
    // 传入空 vector 表示不恢复，保持默认全部可见）
    if (!visibility.isEmpty() && visibility.size() == m_visible.size()) {
        m_visible = visibility;
        for (int f = 0; f < m_graphs.size(); ++f)
            m_graphs.at(f)->setVisible(m_visible.at(f));
    }
    refreshGraphs();
    fitYToVisible();
}

void PlotWidget::clearSource()
{
    m_data = nullptr;
    m_cfg = nullptr;
    m_loadedStep = 0;
    m_loadedKeyRange = QCPRange();
    m_loadedRaw = false;
    m_dataYRangeValid = false;
    buildGraphs();
    replot();
}

void PlotWidget::buildGraphs()
{
    // 必须先删除旧的吸附点：clearGraphs() 只清理 plottable（曲线），
    // QCPItemTracer 属于 item，不显式 removeItem 会一直挂在 QCustomPlot::mItems 里，
    // 每次重建泄漏 25 个 item 并使其持续参与绘制。
    for (QCPItemTracer *tracer : m_tracers) {
        if (tracer)
            removeItem(tracer);
    }
    m_tracers.clear();
    m_graphs.clear();
    m_visible.clear();
    clearGraphs();
    if (m_cursorLine)
        m_cursorLine->setVisible(false);
    hideHover();

    const int count = m_cfg ? m_cfg->fields.size() : 0;
    // 偏差向量按字段数对齐（重建曲线时保留已有偏差，新增字段补 0）
    if (m_bias.size() != count)
        m_bias.resize(count);
    for (int i = 0; i < count; ++i) {
        QCPGraph *graph = addGraph(xAxis, yAxis);
        graph->setName(m_cfg->fields.at(i).name);
        graph->setLineStyle(QCPGraph::lsLine);
        graph->setScatterStyle(QCPScatterStyle::ssNone);
        graph->setSelectable(QCP::stNone);
        graph->setAdaptiveSampling(false);
        m_graphs.append(graph);
        m_visible.append(true);

        QCPItemTracer *tracer = new QCPItemTracer(this);
        tracer->setStyle(QCPItemTracer::tsCircle);
        tracer->setSize(5);
        tracer->setPen(QPen(m_cfg->fields.at(i).color));
        tracer->setBrush(QBrush(Qt::white));
        tracer->setClipToAxisRect(true);
        tracer->setClipAxisRect(axisRect());
        tracer->setVisible(false);
        // 吸附点不绑定 graph（绑定后位置由曲线数据决定，抽稀会使其偏离真实帧），
        // 而是在 updateHover 中按 DataSet 的精确坐标定位。
        m_tracers.append(tracer);
    }
    applyDisplayToGraphs();
    limitXRange();
}

/*!
 * \brief 按“当前可视窗口 + 余量”把数据装载到曲线，超出上限时按步长抽稀
 *
 * 设计要点：
 *  - 绘图控件只持有当前视野附近的数据，内存被限制在
 *    `曲线数 × maxPointsPerCurve × 16 字节`（默认约 8 MB），与文件大小无关；
 *  - 全览时按桶抽取**最小值和最大值**（而不是每 N 帧只取 1 点），尖峰不会被丢弃；
 *    放大后窗口变小、桶宽自动恢复到 1，因此局部细节仍是全分辨率；
 *  - 悬停数值、CSV 导出、Y 轴统计等**始终取自 DataSet**，不受抽稀影响；
 *  - 已装载窗口已覆盖当前视野且桶宽足够细时直接跳过，避免无谓重取。
 */
void PlotWidget::refreshGraphs()
{
    if (!m_data || !m_cfg || m_data->isEmpty() || m_cfg->fields.size() != m_graphs.size()) {
        replot();
        return;
    }
    const int frames = m_data->frameCount();
    const int fields = m_cfg->fields.size();

    // 目标窗口：可视范围左右各留 25% 余量，轻微平移/缩放不必重新装载
    const QCPRange view = xAxis->range();
    const double margin = qMax(1.0, view.size() * 0.25);
    QCPRange window;
    window.lower = qMax(0.0, view.lower - margin);
    window.upper = qMin(double(frames - 1), view.upper + margin);
    if (window.upper < window.lower)
        window.upper = window.lower;

    const int maxPoints = qMax(1000, m_display.maxPointsPerCurve);
    const double windowFrames = window.upper - window.lower + 1.0;
    // 每个抽稀桶最多保留 min/max 两个点（保留尖峰），因此按 maxPoints/2 估算桶宽。
    // 预留 2 个点给窗口首末帧，保证端点精确落在数据边界上。
    const int pointBudget = qMax(2, maxPoints - 2);
    const int step = qMax(1, int(std::ceil(windowFrames / double(qMax(1, pointBudget / 2)))));

    // 已装载的数据若更细（step 更小）且完全覆盖窗口、数值模式与偏差模式一致，则无需重载
    if (m_loadedStep > 0 && m_loadedRaw == m_showRaw && m_loadedBias == m_showBias
        && m_loadedStep <= step && m_loadedKeyRange.lower <= window.lower
        && m_loadedKeyRange.upper >= window.upper) {
        return;
    }

    int first = qMax(0, int(std::floor(window.lower)));
    int last = qMin(frames - 1, int(std::ceil(window.upper)));
    if (last <= first)
        first = qMax(0, last - 1);

    const bool adaptiveSampling = frames > m_display.decimationThreshold;
    for (int f = 0; f < fields; ++f) {
        QVector<double> keys;
        QVector<double> values;
        if (step <= 1) {
            // 窗口足够小：逐帧全分辨率装载
            keys.reserve(last - first + 1);
            values.reserve(last - first + 1);
            for (int i = first; i <= last; ++i) {
                keys.append(double(i));
                values.append(displayValue(i, f));
            }
        } else {
            // 抽稀：每个桶保留最小值和最大值两个点（而不是每 N 帧只取 1 点），
            // 避免尖峰/毛刺在全局视图下被整段丢弃；首末帧额外保留。
            keys.reserve(pointBudget + 4);
            values.reserve(pointBudget + 4);
            int bucketStart = first;
            while (bucketStart <= last) {
                const int bucketEnd = qMin(last, bucketStart + step - 1);
                int minIndex = bucketStart;
                int maxIndex = bucketStart;
                double minValue = displayValue(bucketStart, f);
                double maxValue = minValue;
                for (int i = bucketStart + 1; i <= bucketEnd; ++i) {
                    const double v = displayValue(i, f);
                    if (v < minValue) {
                        minValue = v;
                        minIndex = i;
                    }
                    if (v > maxValue) {
                        maxValue = v;
                        maxIndex = i;
                    }
                }
                int picked[4] = {minIndex, maxIndex, -1, -1};
                int pickedCount = 2;
                if (bucketStart == first)
                    picked[pickedCount++] = bucketStart;
                if (bucketEnd == last)
                    picked[pickedCount++] = bucketEnd;
                std::sort(picked, picked + pickedCount);
                for (int i = 0; i < pickedCount; ++i) {
                    if (i > 0 && picked[i] == picked[i - 1])
                        continue; // 同一帧同时是最小/最大值或首末帧时去重
                    keys.append(double(picked[i]));
                    values.append(displayValue(picked[i], f));
                }
                bucketStart = bucketEnd + 1;
            }
        }
        QCPGraph *graph = m_graphs.at(f);
        graph->setData(keys, values, true);
        graph->setAdaptiveSampling(adaptiveSampling);
        graph->setVisible(m_visible.value(f, true));
        // 说明：吸附点（tracer）不再绑定 graph，而是在 updateHover 中按精确坐标定位，
        // 这样即使曲线经过抽稀也不会出现“提示数值与吸附点不一致”。
    }
    m_loadedStep = step;
    m_loadedKeyRange = window;
    m_loadedRaw = m_showRaw;
    m_loadedBias = m_showBias;
    replot(QCustomPlot::rpQueuedReplot);
}

void PlotWidget::scheduleReload()
{
    if (m_reloadTimer)
        m_reloadTimer->start();
}

void PlotWidget::setDisplayConfig(const DisplayConfig &cfg)
{
    const bool oldRaw = m_showRaw;
    const bool oldBias = m_showBias;
    const int oldMaxPoints = m_display.maxPointsPerCurve;
    m_display = cfg;
    m_showRaw = cfg.showRawValues && hasCorrectedFields();
    m_showBias = cfg.showBias;
    // 数值模式、偏差模式或抽稀上限变化都需要重新装载
    const bool needReload = (m_showRaw != oldRaw) || (m_showBias != oldBias)
                            || (cfg.maxPointsPerCurve != oldMaxPoints);
    if (needReload)
        m_loadedStep = 0;
    applyDisplayToGraphs();
    if (hasData() && needReload)
        refreshGraphs();
    else
        replot(QCustomPlot::rpQueuedReplot);
}

bool PlotWidget::hasCorrectedFields() const
{
    if (!m_cfg)
        return false;
    for (const FieldDef &f : m_cfg->fields) {
        if (f.correct != CorrectSource::None)
            return true;
    }
    return false;
}

double PlotWidget::displayValue(int frame, int field) const
{
    if (!m_data)
        return 0.0;
    double v = m_showRaw ? m_data->rawValue(frame, field) : m_data->value(frame, field);
    if (m_showBias)
        v += m_bias.value(field, 0.0); // 手动偏差叠加（默认 0）
    return v;
}

void PlotWidget::setFieldBias(int fieldIndex, double value)
{
    if (fieldIndex < 0 || fieldIndex >= m_bias.size() || !std::isfinite(value))
        return;
    if (m_bias.at(fieldIndex) == value)
        return;
    m_bias[fieldIndex] = value;
    // 仅在偏差显示开启时已装载数据才会受影响，需要重新装载；关闭时偏差不参与显示
    if (m_showBias)
        m_loadedStep = 0;
}

void PlotWidget::setShowBias(bool show)
{
    if (m_showBias == show && m_display.showBias == show)
        return;
    m_showBias = show;
    m_display.showBias = show;
    m_loadedStep = 0; // 偏差模式变化 → 重新装载
    if (hasData()) {
        fitYToVisible(); // 内部按当前窗口重新装载并自适应 Y 轴
    } else {
        replot(QCustomPlot::rpQueuedReplot);
    }
    AppSettings::saveDisplayConfig(m_display);
}

void PlotWidget::setShowRawValues(bool showRaw)
{
    const bool effective = showRaw && hasCorrectedFields();
    if (effective == m_showRaw && m_display.showRawValues == showRaw)
        return;
    m_showRaw = effective;
    m_display.showRawValues = showRaw;
    m_loadedStep = 0; // 数值模式变化 → 重新装载
    if (hasData()) {
        fitYToVisible(); // 内部会按当前窗口重新装载并自适应 Y 轴
    } else {
        replot(QCustomPlot::rpQueuedReplot);
    }
    AppSettings::saveDisplayConfig(m_display);
}

void PlotWidget::applyDisplayToGraphs()
{
    for (int f = 0; f < m_graphs.size(); ++f) {
        QCPGraph *graph = m_graphs.at(f);
        const QColor color = m_cfg && f < m_cfg->fields.size() ? m_cfg->fields.at(f).color
                                                              : QColor(Qt::black);
        graph->setPen(QPen(color, m_display.lineWidth));
        graph->setAdaptiveSampling(hasData()
                                   && m_data->frameCount() > m_display.decimationThreshold);
    }
    xAxis->grid()->setVisible(m_display.gridVisible);
    yAxis->grid()->setVisible(m_display.gridVisible);
    xAxis->grid()->setSubGridVisible(m_display.gridVisible && m_display.subGridVisible);
    yAxis->grid()->setSubGridVisible(m_display.gridVisible && m_display.subGridVisible);
    setAntialiasedElements(m_display.antialiasing ? QCP::aeAll : QCP::aeNone);
    if (m_display.openGl != openGl())
        setOpenGl(m_display.openGl);
    replot(QCustomPlot::rpQueuedReplot);
}

bool PlotWidget::applyCurveVisible(int fieldIndex, bool visible)
{
    if (fieldIndex < 0 || fieldIndex >= m_graphs.size())
        return false;
    if (m_visible.at(fieldIndex) == visible)
        return false;
    m_visible[fieldIndex] = visible;
    m_graphs.at(fieldIndex)->setVisible(visible);
    if (!visible && fieldIndex < m_tracers.size())
        m_tracers.at(fieldIndex)->setVisible(false);
    emit visibilityChanged(fieldIndex, visible);
    return true;
}

void PlotWidget::setCurveVisible(int fieldIndex, bool visible)
{
    if (!applyCurveVisible(fieldIndex, visible))
        return;
    fitYToVisible(); // 单条切换：立即自适应 Y 轴（内部会重绘）
}

bool PlotWidget::isCurveVisible(int fieldIndex) const
{
    if (fieldIndex < 0 || fieldIndex >= m_visible.size())
        return false;
    return m_visible.at(fieldIndex);
}

void PlotWidget::setAllVisible(bool visible)
{
    bool changed = false;
    for (int f = 0; f < m_graphs.size(); ++f)
        changed = applyCurveVisible(f, visible) || changed;
    if (changed)
        fitYToVisible(); // 批量操作只在最后自适应一次，避免 O(n²) 全列扫描
}

void PlotWidget::applyPreset(Preset preset)
{
    if (!m_cfg)
        return;
    bool changed = false;
    for (int f = 0; f < m_cfg->fields.size(); ++f) {
        const FieldGroup group = m_cfg->fields.at(f).group;
        bool visible = false;
        switch (preset) {
        case PresetAngle:
            visible = (group == FieldGroup::Angle);
            break;
        case PresetRate:
            visible = (group == FieldGroup::Rate);
            break;
        case PresetGyro:
            visible = (group == FieldGroup::Gyro);
            break;
        case PresetAll:
            visible = true;
            break;
        case PresetNone:
            visible = false;
            break;
        }
        changed = applyCurveVisible(f, visible) || changed;
    }
    Q_UNUSED(changed)
    resetView(); // 预设按钮语义：切换显隐后复位视图（内部统一自适应 Y 轴并重绘）
}

void PlotWidget::limitXRange()
{
    if (!hasData())
        return;
    const double lastFrame = double(m_data->frameCount() - 1);
    if (lastFrame <= 0.0) {
        xAxis->setRange(0.0, 1.0);
        return;
    }
    const QCPRange old = xAxis->range();
    const double span = qBound(1.0, old.size(), lastFrame + 1.0); // 最小放大到单帧级，最大不超过全局
    // 夹取时保持视野中心不变：否则到达最小跨度后继续滚轮会让视图持续漂移
    const double center = old.center();
    double lower = center - span / 2.0;
    double upper = center + span / 2.0;
    if (lower < -0.5) {
        lower = -0.5;
        upper = lower + span;
    }
    if (upper > lastFrame + 0.5) {
        upper = lastFrame + 0.5;
        lower = upper - span;
    }
    if (lower != old.lower || upper != old.upper)
        xAxis->setRange(lower, upper);
}

void PlotWidget::resetView()
{
    if (!hasData()) {
        xAxis->setRange(0, 1);
        yAxis->setRange(0, 1);
        replot();
        return;
    }
    xAxis->setRange(-0.5, double(m_data->frameCount() - 1) + 0.5);
    fitYToVisible();
}

void PlotWidget::fitYToVisible()
{
    if (!hasData()) {
        yAxis->setRange(0, 1);
        replot(QCustomPlot::rpQueuedReplot);
        return;
    }
    refreshGraphs(); // 确保当前窗口的数据已装载，Y 轴自适应才准确（已覆盖窗口时内部直接返回）
    bool found = false;
    QCPRange total;
    const QCPRange keyRange = xAxis->range();
    for (int f = 0; f < m_graphs.size(); ++f) {
        if (!m_visible.value(f, false))
            continue;
        bool graphFound = false;
        const QCPRange r = m_graphs.at(f)->getValueRange(graphFound, QCP::sdBoth, keyRange);
        if (!graphFound)
            continue;
        if (!found) {
            total = r;
            found = true;
        } else {
            total.expand(r);
        }
    }
    if (!found) {
        m_dataYRangeValid = false; // 无可见曲线时不得沿用旧数据范围约束平移
        yAxis->setRange(0, 1);
    } else {
        m_dataYRange = total;          // 记录数据实际范围，供 Y 轴平移边界使用
        m_dataYRangeValid = true;
        double margin = total.size() * 0.05;
        if (margin <= 0.0)
            margin = qMax(1.0, qAbs(total.upper) * 0.05);
        yAxis->setRange(total.lower - margin, total.upper + margin);
    }
    emit viewChanged();
    replot(QCustomPlot::rpQueuedReplot);
}

/*!
 * \brief 限制 Y 轴平移范围：始终让视野与数据范围保持交集
 *
 * 否则右键拖动可以把曲线整体拖出视野（与“滚轮把 Y 拉飞”属于同一类问题），
 * 用户会觉得“视图丢了、恢复不回去”。
 */
void PlotWidget::limitYRange()
{
    if (!hasData() || !m_dataYRangeValid)
        return;
    const QCPRange old = yAxis->range();
    const double span = old.size();
    if (!(span > 0.0) || !std::isfinite(span))
        return;
    const double minLower = m_dataYRange.lower - span; // 最多允许数据刚好落在视野边缘
    const double maxLower = m_dataYRange.upper;
    const double lower = qBound(minLower, old.lower, maxLower);
    if (lower != old.lower)
        yAxis->setRange(lower, lower + span);
}

bool PlotWidget::exportPng(const QString &path, QString *error)
{
    if (error)
        error->clear();
    const int width = qMax(320, m_display.pngWidth);
    const int height = qMax(240, m_display.pngHeight);
    if (!savePng(path, width, height, 1.0, -1, 96)) {
        if (error)
            *error = QStringLiteral("导出 PNG 失败，请检查路径是否可写：%1").arg(path);
        return false;
    }
    return true;
}

void PlotWidget::mousePressEvent(QMouseEvent *event)
{
    const QPoint pos = event->pos();
    const bool inside = axisRect()->rect().contains(pos);
    // 无数据时不允许框选/平移（此时没有可缩放的帧范围）
    if (hasData() && event->button() == Qt::LeftButton && inside) {
        m_banding = true;
        m_pressPos = pos;
        m_rubberBand->setGeometry(QRect(pos, QSize()));
        m_rubberBand->show();
        return;
    }
    if (hasData() && event->button() == Qt::RightButton && inside) {
        m_panning = true;
        m_pressPos = pos;
        m_panXRange = xAxis->range();
        m_panYRange = yAxis->range();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    QCustomPlot::mousePressEvent(event);
}

void PlotWidget::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->pos();
    if (m_banding) {
        m_rubberBand->setGeometry(QRect(m_pressPos, pos).normalized());
    } else if (m_panning) {
        panTo(pos);
    }
    updateHover(pos);
    QCustomPlot::mouseMoveEvent(event);
}

void PlotWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_banding && event->button() == Qt::LeftButton) {
        m_banding = false;
        const QRect rect = m_rubberBand->geometry();
        m_rubberBand->hide();
        if (rect.width() > 5 && rect.height() > 5) {
            const double x1 = xAxis->pixelToCoord(rect.left());
            const double x2 = xAxis->pixelToCoord(rect.right());
            const double y1 = yAxis->pixelToCoord(rect.top());
            const double y2 = yAxis->pixelToCoord(rect.bottom());
            xAxis->setRange(qMin(x1, x2), qMax(x1, x2));
            yAxis->setRange(qMin(y1, y2), qMax(y1, y2));
            limitXRange();
            emit viewChanged();
            scheduleReload(); // 窗口变了 → 去抖重载曲线数据
            replot();
        }
        return;
    }
    if (m_panning && event->button() == Qt::RightButton) {
        m_panning = false;
        unsetCursor();
        emit viewChanged();
        scheduleReload();
        replot();
        return;
    }
    QCustomPlot::mouseReleaseEvent(event);
}

void PlotWidget::wheelEvent(QWheelEvent *event)
{
    QCustomPlot::wheelEvent(event); // 内置实现：以光标位置为中心缩放（仅 X 轴，见构造函数）
    limitXRange();
    // Y 轴随可视窗口自适应：既保证放大后能看到局部细节，也避免 Y 被滚轮无限放大
    fitYToVisible();
    emit viewChanged();
    if (m_hovering)
        updateHover(event->position().toPoint());
}

void PlotWidget::leaveEvent(QEvent *event)
{
    hideHover();
    emit cursorMoved(-1, 0.0, false);
    QCustomPlot::leaveEvent(event);
}

void PlotWidget::resizeEvent(QResizeEvent *event)
{
    QCustomPlot::resizeEvent(event);
    layoutHoverLabel();
}

void PlotWidget::panTo(const QPoint &pos)
{
    // 与 QCustomPlot 内置拖动一致的换算方式
    const double xDiff = xAxis->pixelToCoord(m_pressPos.x()) - xAxis->pixelToCoord(pos.x());
    const double yDiff = yAxis->pixelToCoord(m_pressPos.y()) - yAxis->pixelToCoord(pos.y());
    xAxis->setRange(m_panXRange.lower + xDiff, m_panXRange.upper + xDiff);
    yAxis->setRange(m_panYRange.lower + yDiff, m_panYRange.upper + yDiff);
    limitXRange();
    limitYRange(); // 不允许把曲线整体拖出视野
    replot(QCustomPlot::rpQueuedReplot);
    scheduleReload(); // 平移后按新窗口重新装载（去抖，拖动过程中不会频繁重取）
    emit viewChanged();
}

void PlotWidget::updateHover(const QPoint &pos)
{
    if (!hasData() || !m_cfg || !axisRect()->rect().contains(pos)) {
        hideHover();
        emit cursorMoved(-1, 0.0, false);
        return;
    }
    m_hovering = true;

    const int lastFrame = m_data->frameCount() - 1;
    int frame = int(qRound(xAxis->pixelToCoord(pos.x())));
    frame = qBound(0, frame, lastFrame);

    QStringList lines;
    QString modeTag = m_showRaw ? QStringLiteral("（未修正原始值）")
                                : QStringLiteral("（惯导修正后）");
    if (m_showBias)
        modeTag += QStringLiteral("＋手动偏差");
    lines << QStringLiteral("<b>帧序号 %1</b>%2").arg(frame).arg(modeTag);

    // 可见曲线很多时提示框会非常高（25 条 ≈ 26 行），遮挡曲线且在小屏上超出轴域。
    // 策略：优先显示与光标高度最接近的若干条曲线（用户通常在看鼠标附近的那条），
    // 其余折叠为一行提示；吸附点仍然为所有可见曲线显示。
    constexpr int kMaxHoverLines = 12;
    const double cursorY = yAxis->pixelToCoord(pos.y());

    QVector<int> visibleFields;
    QVector<double> visibleValues;
    for (int f = 0; f < m_graphs.size(); ++f) {
        if (!m_visible.value(f, false))
            continue;
        const double value = displayValue(frame, f);
        visibleFields.append(f);
        visibleValues.append(value);
        if (f < m_tracers.size()) {
            // 吸附点按 DataSet 的精确值定位（不绑定 graph），因此曲线抽稀不影响提示准确性
            m_tracers.at(f)->position->setCoords(double(frame), value);
            m_tracers.at(f)->setVisible(true);
        }
    }

    const int total = visibleFields.size();
    if (total > kMaxHoverLines) {
        std::sort(visibleFields.begin(), visibleFields.end(), [&](int a, int b) {
            return qAbs(displayValue(frame, a) - cursorY) < qAbs(displayValue(frame, b) - cursorY);
        });
        visibleFields.resize(kMaxHoverLines);
        std::sort(visibleFields.begin(), visibleFields.end()); // 保持字段顺序，避免数值跳动
    }

    int shown = 0;
    for (int f : visibleFields) {
        const FieldDef &def = m_cfg->fields.at(f);
        const double value = displayValue(frame, f);
        const QString unit = def.unit.isEmpty() ? QString() : QStringLiteral(" %1").arg(def.unit);
        lines << QStringLiteral("<span style=\"color:%1\">&#9632;</span> %2: <b>%3</b>%4")
                     .arg(def.color.name(), def.name, QString::number(value, 'f', 2), unit);
        ++shown;
    }
    if (total > kMaxHoverLines)
        lines << QStringLiteral("<i>… 另有 %1 条曲线未显示（可减少勾选）</i>")
                     .arg(total - kMaxHoverLines);
    if (shown == 0)
        lines << QStringLiteral("<i>当前无可见曲线</i>");

    m_hoverLabel->setText(lines.join(QStringLiteral("<br>")));
    m_hoverLabel->adjustSize();
    m_hoverLabel->show();
    m_hoverLabel->raise();
    layoutHoverLabel();

    const QCPRange range = yAxis->range();
    m_cursorLine->start->setCoords(double(frame), range.lower);
    m_cursorLine->end->setCoords(double(frame), range.upper);
    m_cursorLine->setVisible(true);

    emit cursorMoved(frame, yAxis->pixelToCoord(pos.y()), true);
    replot(QCustomPlot::rpQueuedReplot);
}

void PlotWidget::hideHover()
{
    // 即使 m_hovering 已为 false，也强制清理所有悬停元素：
    // 重建曲线/清空数据源时可能残留已显示的吸附点，避免其继续参与绘制。
    const bool wasVisible = m_hovering;
    m_hovering = false;
    if (m_hoverLabel)
        m_hoverLabel->hide();
    if (m_cursorLine)
        m_cursorLine->setVisible(false);
    for (QCPItemTracer *tracer : m_tracers)
        tracer->setVisible(false);
    if (wasVisible)
        replot(QCustomPlot::rpQueuedReplot);
}

void PlotWidget::layoutHoverLabel()
{
    if (!m_hoverLabel || !m_hovering)
        return;
    const QRect rect = axisRect()->rect();
    const QSize size = m_hoverLabel->sizeHint();
    int x = rect.right() - size.width() - 8;
    int y = rect.top() + 8;
    x = qMax(rect.left() + 4, x);
    y = qMin(y, qMax(rect.top() + 4, rect.bottom() - size.height() - 4));
    m_hoverLabel->move(x, y);
}

} // namespace xn
