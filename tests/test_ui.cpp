/*!
 * \file test_ui.cpp
 * \brief 界面集成测试：主窗口装载数据、复选框显隐联动、预设按钮、抽稀、悬停提示、PNG 导出。
 *
 * 使用 54 字节双惯导样例（3841 帧），默认修正来源为 IMU5136。
 * 配置读写被隔离到临时 INI 目录，避免污染/依赖用户真实设置。
 *
 * 无显示环境下运行：xw_ui_tests.exe -platform offscreen
 */

#include <QtTest>

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QGridLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

#include "core/appsettings.h"
#include "core/protocol.h"
#include "ui/curvepanel.h"
#include "ui/mainwindow.h"
#include "ui/plotwidget.h"

#include "samplelocator.h"

using namespace xn;

namespace {

/*! \brief 定位 54 字节双惯导样例（见 samplelocator.h；可用环境变量 XW_SAMPLE 指定） */
QString locateSampleFile()
{
    return xwtest::locateDualSampleFile();
}

QCheckBox *findCheckBox(CurvePanel *panel, const QString &name)
{
    const QList<QCheckBox *> boxes = panel->findChildren<QCheckBox *>();
    for (QCheckBox *box : boxes) {
        if (box->text() == name)
            return box;
    }
    return nullptr;
}

QPushButton *findButton(QWidget *root, const QString &text)
{
    const QList<QPushButton *> buttons = root->findChildren<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}

/*! \brief 定位某分组(groupTitle)第 row 行（0=首个数据行）的偏差输入框 */
QDoubleSpinBox *findBiasEdit(CurvePanel *panel, const QString &groupTitle, int row)
{
    const QList<QGroupBox *> groupBoxes = panel->findChildren<QGroupBox *>();
    for (QGroupBox *gb : groupBoxes) {
        if (gb->title() != groupTitle)
            continue;
        auto *grid = qobject_cast<QGridLayout *>(gb->layout());
        if (!grid)
            continue;
        QLayoutItem *item = grid->itemAtPosition(row + 1, 2); // 第 0 行是“偏差”表头
        if (item)
            return qobject_cast<QDoubleSpinBox *>(item->widget());
    }
    return nullptr;
}

int visibleCount(const PlotWidget *plot)
{
    int count = 0;
    for (int f = 0; f < plot->curveCount(); ++f) {
        if (plot->isCurveVisible(f))
            ++count;
    }
    return count;
}

} // namespace

/*! \brief 界面集成测试 */
class UiTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void loadsAndPlotsSample();
    void checkboxTogglesCurve();
    void checkboxFieldIndexMapping();
    void presetButtonsSyncCheckBoxes();
    void rawAndCorrectedValueToggle();
    void biasInputAndToggle();
    void windowedLoadingAndDecimation();
    void rebuildDoesNotLeakItems();
    void hoverPanelPrefersCurvesNearCursor();
    void wheelZoomKeepsAxesBounded();
    void pngExport();

private:
    MainWindow *m_window = nullptr;
    PlotWidget *m_plot = nullptr;
    CurvePanel *m_panel = nullptr;
    QTemporaryDir *m_settingsDir = nullptr;
};

void UiTest::initTestCase()
{
    const QString sample = locateSampleFile();
    if (sample.isEmpty())
        QSKIP("未找到 54 字节 sample 测试数据文件（可用环境变量 XW_SAMPLE 指定）");

    // 必须在 MainWindow / AppSettings 首次构造 QSettings 之前完成隔离：
    // 使用临时目录下的 INI，测试既不读取用户上一次的显示设置，
    // 也不会把测试期间的偏差/数值模式写回真实配置。
    m_settingsDir = new QTemporaryDir;
    QVERIFY(m_settingsDir->isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir->path());

    m_window = new MainWindow();
    m_window->resize(1280, 820);
    m_window->show();
    QVERIFY(m_window->openPath(sample));
    QApplication::processEvents();

    m_plot = m_window->findChild<PlotWidget *>();
    m_panel = m_window->findChild<CurvePanel *>();
    QVERIFY(m_plot);
    QVERIFY(m_panel);

    DisplayConfig cfg; // 默认：修正后数值、每曲线 20000 点
    m_plot->setDisplayConfig(cfg);
    m_plot->setAllVisible(true);
    m_plot->resetView();
    QApplication::processEvents();
}

void UiTest::cleanupTestCase()
{
    if (m_plot)
        m_plot->setShowRawValues(false);
    delete m_window;
    m_window = nullptr;
    m_plot = nullptr;
    m_panel = nullptr;

    // 清空隔离配置后再删除临时目录，避免 QSettings 析构时回写到已删除路径。
    AppSettings::resetAll();
    delete m_settingsDir;
    m_settingsDir = nullptr;
}

void UiTest::loadsAndPlotsSample()
{
    QCOMPARE(m_plot->curveCount(), 25);
    QVERIFY(m_plot->hasData());
    QCOMPARE(visibleCount(m_plot), 25);
    // 25 个曲线复选框全部建立且默认勾选
    const QList<QCheckBox *> boxes = m_panel->findChildren<QCheckBox *>();
    QCOMPARE(boxes.size(), 25);
    for (QCheckBox *box : boxes)
        QVERIFY(box->isChecked());
    // 默认坐标系范围覆盖全部 3841 帧（54 字节样例）
    QCOMPARE(m_plot->xAxis->range().size(), 3841.0);
}

void UiTest::checkboxTogglesCurve()
{
    QCheckBox *box = findCheckBox(m_panel, QStringLiteral("方位目标角度"));
    QVERIFY(box);
    box->click(); // 复选框 -> 主窗口 -> 绘图控件 单向联动
    QApplication::processEvents();
    QVERIFY(!m_plot->isCurveVisible(0));
    QCOMPARE(visibleCount(m_plot), 24);

    box->click();
    QApplication::processEvents();
    QVERIFY(m_plot->isCurveVisible(0));
    QCOMPARE(visibleCount(m_plot), 25);
}

/*!
 * \brief 回归测试：复选框按“字段序号”与曲线一一对应
 *
 * 界面按分组（角度/速度/惯导/信标）排列复选框，控件创建顺序与字段序号并不一致
 * （例如“俯仰运行速度”是字段 6，却是速度组的第 3 个控件）。
 * 若混用两种下标，会出现“界面勾选了却没有曲线 / 勾选后曲线反而消失”的问题。
 */
void UiTest::checkboxFieldIndexMapping()
{
    // 字段序号对照：2=方位运行速度 3=方位前馈速度 6=俯仰运行速度 7=俯仰前馈速度
    QCheckBox *pitchSpeed = findCheckBox(m_panel, QStringLiteral("俯仰运行速度"));
    QCheckBox *pitchFeed = findCheckBox(m_panel, QStringLiteral("俯仰前馈速度"));
    QVERIFY(pitchSpeed);
    QVERIFY(pitchFeed);

    pitchSpeed->click(); // 取消勾选字段 6
    QApplication::processEvents();
    QVERIFY(!m_plot->isCurveVisible(6));
    QVERIFY(!m_panel->isChecked(6));
    QVERIFY(m_plot->isCurveVisible(2)); // 同组的其它字段不受影响
    QVERIFY(m_panel->isChecked(2));
    QVERIFY(m_plot->isCurveVisible(7));
    QVERIFY(m_panel->isChecked(7));

    pitchFeed->click(); // 取消勾选字段 7
    QApplication::processEvents();
    QVERIFY(!m_plot->isCurveVisible(7));
    QVERIFY(!m_panel->isChecked(7));
    QVERIFY(m_plot->isCurveVisible(3)); // 方位前馈速度（速度组第 2 个控件）不受影响
    QVERIFY(m_panel->isChecked(3));

    // 只勾选这两个字段时，绘图控件必须正好显示 2 条曲线且就是字段 6、7
    QPushButton *clear = findButton(m_panel, QStringLiteral("清空全部"));
    QVERIFY(clear);
    clear->click();
    QApplication::processEvents();
    QCOMPARE(visibleCount(m_plot), 0);
    pitchSpeed->click();
    pitchFeed->click();
    QApplication::processEvents();
    QCOMPARE(visibleCount(m_plot), 2);
    QVERIFY(m_plot->isCurveVisible(6));
    QVERIFY(m_plot->isCurveVisible(7));
    for (int f = 0; f < 25; ++f) {
        const bool expected = (f == 6 || f == 7);
        QCOMPARE(m_panel->isChecked(f), expected); // 复选框状态与曲线状态完全一致
    }

    // 恢复默认全选，避免影响后续用例
    QPushButton *all = findButton(m_panel, QStringLiteral("全量显示"));
    QVERIFY(all);
    all->click();
    QApplication::processEvents();
    QCOMPARE(visibleCount(m_plot), 25);
}

void UiTest::presetButtonsSyncCheckBoxes()
{
    QPushButton *clear = findButton(m_panel, QStringLiteral("清空全部"));
    QVERIFY(clear);
    clear->click();
    QApplication::processEvents();
    QCOMPARE(visibleCount(m_plot), 0);
    for (int f = 0; f < 25; ++f)
        QVERIFY(!m_panel->isChecked(f)); // 绘图控件 -> 复选框 反向同步

    QPushButton *angle = findButton(m_panel, QStringLiteral("角度对比"));
    QVERIFY(angle);
    angle->click();
    QApplication::processEvents();
    QCOMPARE(visibleCount(m_plot), 6); // 三轴目标角度 + 三轴编码器角度
    for (int f = 0; f < 25; ++f) {
        const bool isAngle = (f % 4 == 0 || f % 4 == 1) && f < 12;
        QCOMPARE(m_plot->isCurveVisible(f), isAngle);
        QCOMPARE(m_panel->isChecked(f), isAngle);
    }

    QPushButton *all = findButton(m_panel, QStringLiteral("全量显示"));
    QVERIFY(all);
    all->click();
    QApplication::processEvents();
    QCOMPARE(visibleCount(m_plot), 25);
}

void UiTest::rawAndCorrectedValueToggle()
{
    // 帧 0 的方位目标角度（默认仅 IMU 修正）：307.87 + (-0.69) = 307.18°
    const double corrected = m_plot->graph(0)->data()->at(0)->value;
    QCOMPARE(corrected, 307.18);
    QVERIFY(!m_plot->showRawValues());

    m_plot->setShowRawValues(true);
    QApplication::processEvents();
    QVERIFY(m_plot->showRawValues());
    QCOMPARE(m_plot->graph(0)->data()->at(0)->value, 307.87); // 未修正原始值

    // 未参与修正的字段在两种模式下数值相同：横滚编码器角度
    QCOMPARE(m_plot->graph(9)->data()->at(0)->value, 2.80);

    m_plot->setShowRawValues(false);
    QApplication::processEvents();
    QVERIFY(!m_plot->showRawValues());
    QCOMPARE(m_plot->graph(0)->data()->at(0)->value, 307.18);
}

/*!
 * \brief 手动偏差：每行一个偏差框、同时加到该行字段，开关切换对比
 */
void UiTest::biasInputAndToggle()
{
    // 共 13 个偏差框（角度3 + 速度3 + 惯导6 + 信标1）
    const QList<QDoubleSpinBox *> edits = m_panel->findChildren<QDoubleSpinBox *>();
    QCOMPARE(edits.size(), 13);

    // 从基础模式开始
    m_plot->setShowBias(false);
    QApplication::processEvents();
    QVERIFY(!m_plot->showBias());

    // 记录帧 0 方位行（字段 0 方位目标、1 方位编码器）的基础值
    const double base0 = m_plot->graph(0)->data()->at(0)->value;
    const double base1 = m_plot->graph(1)->data()->at(0)->value;
    QCOMPARE(base0, 307.18);
    QCOMPARE(base1, 307.86);

    // 通过角度类第 0 行的偏差框输入 +10（模拟用户手动输入，自动保存）
    QDoubleSpinBox *azBias = findBiasEdit(m_panel, QStringLiteral("角度类"), 0);
    QVERIFY(azBias);
    azBias->setValue(10.0);
    QApplication::processEvents();

    // 开关关闭时曲线仍为基础值（偏差已保存但不显示）
    QCOMPARE(m_plot->graph(0)->data()->at(0)->value, base0);

    // 打开偏差显示：同一行字段 0、1 同时 +10
    m_plot->setShowBias(true);
    QApplication::processEvents();
    QVERIFY(m_plot->showBias());
    QCOMPARE(m_plot->graph(0)->data()->at(0)->value, base0 + 10.0);
    QCOMPARE(m_plot->graph(1)->data()->at(0)->value, base1 + 10.0);

    // 改为 -5：同一行两个字段一起更新
    azBias->setValue(-5.0);
    QApplication::processEvents();
    QCOMPARE(m_plot->graph(0)->data()->at(0)->value, base0 - 5.0);
    QCOMPARE(m_plot->graph(1)->data()->at(0)->value, base1 - 5.0);

    // 关闭开关：恢复基础值
    m_plot->setShowBias(false);
    QApplication::processEvents();
    QCOMPARE(m_plot->graph(0)->data()->at(0)->value, base0);
    QCOMPARE(m_plot->graph(1)->data()->at(0)->value, base1);

    // 复位偏差为 0（清理持久化，避免影响后续用例）
    azBias->setValue(0.0);
    QApplication::processEvents();
}

/*!
 * \brief 窗口化抽稀加载：全览时按 min/max 桶抽稀、放大后自动恢复全分辨率
 *
 * 目的：把绘图侧内存限制在 曲线数 × maxPointsPerCurve × 16 字节（与文件大小无关），
 * 同时保证全局视图也保留每个桶的尖峰（最小值/最大值各取一点）。
 */
void UiTest::windowedLoadingAndDecimation()
{
    const DisplayConfig original = m_plot->displayConfig();
    DisplayConfig cfg = original;
    cfg.maxPointsPerCurve = 1000; // 人为压小上限（最小允许值），便于在 3841 帧样例上验证抽稀
    m_plot->setDisplayConfig(cfg);
    m_plot->resetView();
    QApplication::processEvents();

    QCPGraph *graph = m_plot->graph(0);
    QVERIFY(graph);

    // 全览：3841 帧 → 桶宽 8，每桶 min/max 两点，点数被限制在上限以内，且首末帧精确
    const int fullCount = graph->data()->size();
    QVERIFY2(fullCount < 1000,
             qPrintable(QStringLiteral("全览点数=%1（应小于上限 1000）").arg(fullCount)));
    QVERIFY2(fullCount > 500, qPrintable(QStringLiteral("全览点数=%1（应大于 500）").arg(fullCount)));
    QCOMPARE(graph->data()->at(0)->key, 0.0);
    QCOMPARE(graph->data()->at(fullCount - 1)->key, 3840.0);

    // 首末帧数值来自 DataSet（默认 IMU 修正后）：帧 0 = 307.18，帧 3840 = 304.86
    QCOMPARE(graph->data()->at(0)->value, 307.18);
    QCOMPARE(graph->data()->at(fullCount - 1)->value, 304.86);

    // 放大到 41 帧窗口（含 25% 余量后仍小于上限）→ 应以桶宽 1 精确装载
    m_plot->xAxis->setRange(500, 540);
    m_plot->fitYToVisible(); // 内部会按新窗口重新装载
    QApplication::processEvents();

    const int zoomCount = graph->data()->size();
    QVERIFY2(zoomCount >= 61, qPrintable(QStringLiteral("放大后点数=%1").arg(zoomCount)));
    bool stepIsOne = true;
    for (int i = 1; i < zoomCount; ++i) {
        if (!qFuzzyCompare(graph->data()->at(i)->key - graph->data()->at(i - 1)->key, 1.0)) {
            stepIsOne = false;
            break;
        }
    }
    QVERIFY2(stepIsOne, "放大后应恢复逐帧（桶宽 1）装载");
    const double firstKey = graph->data()->at(0)->key;
    QVERIFY(firstKey <= 500.0);
    QVERIFY(graph->data()->at(zoomCount - 1)->key >= 540.0);

    // 放大后数据窗口只覆盖可视范围附近（而不是整个文件）
    QVERIFY(graph->data()->at(zoomCount - 1)->key < 600.0);

    // 帧 500 的键必须精确存在且数值取自 DataSet（抽稀/窗口化不影响数值来源）
    const int idx500 = 500 - int(firstKey);
    QVERIFY(idx500 >= 0 && idx500 < zoomCount);
    QCOMPARE(graph->data()->at(idx500)->key, 500.0);
    QCOMPARE(graph->data()->at(idx500)->value, 300.37); // 帧 500 方位目标（IMU 修正后）

    // 恢复原配置与视图
    m_plot->setDisplayConfig(original);
    m_plot->resetView();
    QApplication::processEvents();
    QCOMPARE(graph->data()->size(), 3841); // 默认上限 20000，全分辨率
    QCOMPARE(graph->data()->at(100)->key, 100.0);
    QCOMPARE(graph->data()->at(100)->value, 296.86); // 帧 100 方位目标（IMU 修正后）
}

/*!
 * \brief 回归测试：反复重新解析不得泄漏绘图对象
 *
 * 历史缺陷：buildGraphs() 只 clear 了 m_tracers 容器（裸指针），没有 removeItem，
 * QCPItemTracer 仍留在 QCustomPlot::mItems 中，每次重建泄漏 25 个 item 并持续参与绘制。
 */
void UiTest::rebuildDoesNotLeakItems()
{
    const int itemsBefore = m_plot->itemCount();
    const int plottablesBefore = m_plot->plottableCount();
    QVERIFY(itemsBefore >= 25 + 1); // 25 个吸附点 + 1 条游标线

    const QString sample = locateSampleFile();
    if (sample.isEmpty())
        QSKIP("未找到 54 字节 sample 测试数据文件");

    for (int i = 0; i < 3; ++i) {
        QVERIFY(m_window->openPath(sample)); // 触发 setSource → buildGraphs
        QApplication::processEvents();
    }
    QCOMPARE(m_plot->itemCount(), itemsBefore);
    QCOMPARE(m_plot->plottableCount(), plottablesBefore);
    QCOMPARE(m_plot->curveCount(), 25);
}

/*!
 * \brief 悬停提示框：25 条曲线全可见时最多 12 行，且优先显示光标附近的曲线
 */
void UiTest::hoverPanelPrefersCurvesNearCursor()
{
    m_plot->setAllVisible(true);
    m_plot->resetView();
    QApplication::processEvents();

    const QList<QLabel *> labels = m_plot->findChildren<QLabel *>();
    QVERIFY(!labels.isEmpty());
    QLabel *hover = labels.first();

    const QRect rect = m_plot->axisRect()->rect();
    const auto hoverAt = [&](int y) {
        const QPoint pos(rect.left() + rect.width() / 3, y);
        QMouseEvent move(QEvent::MouseMove, QPointF(pos), m_plot->mapToGlobal(pos), Qt::NoButton,
                         Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(m_plot, &move);
        QApplication::processEvents();
        return hover->text();
    };

    // 25 条曲线全可见：光标靠近顶部（Y≈最大值 320），信标功率值（约 -63）距光标最远，应被折叠
    const QString top = hoverAt(rect.top() + 6);
    QVERIFY2(!top.contains(QStringLiteral("信标功率值")), qPrintable(top));
    QVERIFY2(top.contains(QStringLiteral("另有 13 条")), qPrintable(top)); // 25 → 只显示 12 条
    QCOMPARE(top.count(QStringLiteral("&#9632;")), 12);                    // 行数上限

    // 隐藏 LINS358 六条曲线后，信标功率值成为全场最小值：
    // 靠近底部（Y≈最小值）时信标距光标最近，必须出现
    for (int f = 18; f <= 23; ++f)
        m_plot->setCurveVisible(f, false);
    QApplication::processEvents();
    const QString bottom = hoverAt(rect.bottom() - 6);
    QVERIFY2(bottom.contains(QStringLiteral("信标功率值")), qPrintable(bottom));
    QVERIFY2(bottom.contains(QStringLiteral("另有 7 条")), qPrintable(bottom)); // 19 → 12
    QCOMPARE(bottom.count(QStringLiteral("&#9632;")), 12);

    m_plot->setAllVisible(true);
    QApplication::processEvents();

    // 减少勾选到 5 条以内后，不再出现折叠提示
    for (int f = 0; f < 25; ++f)
        m_plot->setCurveVisible(f, f < 5);
    QApplication::processEvents();
    const QString few = hoverAt(rect.top() + 6);
    QVERIFY(!few.contains(QStringLiteral("另有")));
    QCOMPARE(few.count(QStringLiteral("&#9632;")), 5);

    m_plot->setAllVisible(true);
    QApplication::processEvents();
}

/*!
 * \brief 回归测试：滚轮缩放不得让坐标轴失控
 *
 * 历史缺陷：滚轮同时缩放 X/Y，而 X 被 limitXRange 夹在 1~帧数 之后不再变化，
 * Y 却每次“缩小”乘 1.176 且没有上界 —— 连续几次就把 Y 拉到 ±1e6，
 * 曲线被压成一条平线，且用户再滚轮也回不去（表现为“缩小以后再放大就恢复不回去”）。
 */
void UiTest::wheelZoomKeepsAxesBounded()
{
    m_plot->resetView();
    QApplication::processEvents();

    const QCPRange yFull = m_plot->yAxis->range();
    const double frames =
        double(m_plot->graph(0)->data()->at(m_plot->graph(0)->data()->size() - 1)->key);
    QVERIFY(yFull.size() > 1.0);
    QVERIFY(frames > 100.0);

    const QRect rect = m_plot->axisRect()->rect();
    const QPoint center = rect.center();
    const auto spin = [&](int steps, int delta) {
        for (int i = 0; i < steps; ++i) {
            QWheelEvent ev(QPointF(center), m_plot->mapToGlobal(center), QPoint(0, 0),
                           QPoint(0, delta), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase,
                           false);
            QApplication::sendEvent(m_plot, &ev);
            QApplication::processEvents();
        }
    };

    // 放大 10 格：X 变窄，Y 自适应到可见窗口（跨度不会超过全局）
    spin(10, +120);
    const QCPRange xIn = m_plot->xAxis->range();
    const QCPRange yIn = m_plot->yAxis->range();
    QVERIFY2(xIn.size() < frames / 2.0, qPrintable(QString::number(xIn.size())));
    QVERIFY2(yIn.size() <= yFull.size() * 1.01, qPrintable(QString::number(yIn.size())));

    // 缩小 10 格：X 必须回到全局视图
    spin(10, -120);
    QCOMPARE(m_plot->xAxis->range().lower, -0.5);
    QCOMPARE(m_plot->xAxis->range().upper, frames + 0.5);

    // 再缩小 50 格：X 保持全局，Y 必须仍然有界（缺陷时会放大到 ±1e6）
    spin(50, -120);
    const QCPRange xOut = m_plot->xAxis->range();
    const QCPRange yOut = m_plot->yAxis->range();
    QCOMPARE(xOut.lower, -0.5);
    QCOMPARE(xOut.upper, frames + 0.5);
    QVERIFY2(yOut.size() <= yFull.size() * 1.01, qPrintable(QString::number(yOut.size())));
    // 曲线仍然在视野内（Y 范围覆盖数据范围）
    QVERIFY(yOut.lower <= yFull.lower + 1e-6);
    QVERIFY(yOut.upper >= yFull.upper - 1e-6);

    // 反复交替放大/缩小后仍保持有界
    for (int i = 0; i < 5; ++i) {
        spin(7, +120);
        spin(9, -120);
    }
    const QCPRange xEnd = m_plot->xAxis->range();
    const QCPRange yEnd = m_plot->yAxis->range();
    QVERIFY(xEnd.lower >= -0.5 - 1e-9 && xEnd.upper <= frames + 0.5 + 1e-9);
    QVERIFY2(yEnd.size() <= yFull.size() * 1.01, qPrintable(QString::number(yEnd.size())));

    m_plot->resetView();
    QApplication::processEvents();
    QCOMPARE(m_plot->yAxis->range().lower, yFull.lower);
    QCOMPARE(m_plot->yAxis->range().upper, yFull.upper);
}

void UiTest::pngExport()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("plot.png"));
    QString error;
    QVERIFY2(m_plot->exportPng(path, &error), qPrintable(error));
    QVERIFY(QFileInfo::exists(path));
    QVERIFY(QFileInfo(path).size() > 10000); // 25 条曲线，图片不应为空
}

QTEST_MAIN(UiTest)
#include "test_ui.moc"
