#include "mainwindow.h"

#include "configdialog.h"
#include "curvepanel.h"
#include "plotwidget.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressBar>
#include <QProgressDialog>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QTextBrowser>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

namespace xn {

namespace {
#ifndef APP_VERSION
#define APP_VERSION "1.0.0" // 测试工程未定义 APP_VERSION 时的兜底（主工程由 XingWang.pro 注入）
#endif
const char *kAppVersion = APP_VERSION;

/*! \brief 十六进制字节显示（两位大写） */
QString hexByte(quint8 value)
{
    return QString::number(value, 16).toUpper().rightJustified(2, QLatin1Char('0'));
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("星网转台数据解析与可视化软件 v%1").arg(QLatin1String(kAppVersion)));

    // 初始窗口尺寸按当前屏幕自适应：取设计尺寸 1360×860 与可用区域 92% 的较小者，
    // 保证在小屏笔记本（如 1366×768）或高 DPI 缩放后的较小逻辑分辨率下也不会超出屏幕。
    QScreen *screen = QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, 1360, 860);
    const int initW = qMin(1360, static_cast<int>(avail.width() * 0.92));
    const int initH = qMin(860, static_cast<int>(avail.height() * 0.92));
    resize(initW, initH);

    m_config = AppSettings::loadFrameConfig();

    createActions();
    createMenus();
    createToolBar();
    createCentralWidget();
    createStatusBar();
    connectSignals();

    m_plot->setDisplayConfig(AppSettings::loadDisplayConfig());
    // 恢复上次的数值显示模式（修正后 / 未修正原始值）
    m_showRawAction->setChecked(m_plot->displayConfig().showRawValues);
    m_curvePanel->build(m_config);

    // 恢复每字段手动偏差并回填到面板（曲线数据加载后再同步到绘图控件）
    m_fieldBias = AppSettings::loadFieldBias(m_config.fields.size());
    for (int f = 0; f < m_fieldBias.size(); ++f)
        m_curvePanel->setBiasSilently(f, m_fieldBias.at(f));
    // 恢复偏差显示开关按钮状态（绘图控件已在 setDisplayConfig 中同步该状态）
    m_showBiasAction->setChecked(m_plot->showBias());

    // 面板内容已建立：按内容所需高度先给一次分栏比例（窗口首次显示时还会再精确算一次）
    applyContentBasedSplitterSizes();

    updateStatus();
    updateActions();

    // 窗口在可用屏幕区域居中
    move(avail.x() + (avail.width() - width()) / 2,
         avail.y() + (avail.height() - height()) / 2);
}

void MainWindow::createActions()
{
    const QStyle *s = style();

    m_openAction = new QAction(s->standardIcon(QStyle::SP_DialogOpenButton),
                               QStringLiteral("打开文件(&O)..."), this);
    m_openAction->setShortcut(QKeySequence::Open);
    m_openAction->setStatusTip(QStringLiteral("打开串口采集的 hex 文本文件并解析"));

    m_reloadAction = new QAction(s->standardIcon(QStyle::SP_BrowserReload),
                                 QStringLiteral("重新解析(&R)"), this);
    m_reloadAction->setShortcut(QKeySequence(Qt::Key_F5));
    m_reloadAction->setStatusTip(QStringLiteral("使用当前协议配置重新解析已加载的文件"));

    m_resetViewAction = new QAction(s->standardIcon(QStyle::SP_FileDialogDetailedView),
                                    QStringLiteral("视图复位(&Z)"), this);
    m_resetViewAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+R")));
    m_resetViewAction->setStatusTip(QStringLiteral("恢复全局视图并自适应 Y 轴"));

    m_exportCsvAction = new QAction(s->standardIcon(QStyle::SP_DialogSaveButton),
                                    QStringLiteral("导出 CSV(&C)..."), this);
    m_exportCsvAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+E")));
    m_exportCsvAction->setStatusTip(QStringLiteral("导出全部解析数据（UTF-8 带 BOM）"));

    m_exportPngAction = new QAction(s->standardIcon(QStyle::SP_DialogSaveButton),
                                    QStringLiteral("导出 PNG(&P)..."), this);
    m_exportPngAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    m_exportPngAction->setStatusTip(QStringLiteral("导出当前图表视图为 PNG 图片"));

    m_settingsAction = new QAction(s->standardIcon(QStyle::SP_FileDialogListView),
                                   QStringLiteral("设置(&S)..."), this);
    m_settingsAction->setStatusTip(QStringLiteral("帧协议、惯导修正与显示配置"));

    m_showRawAction = new QAction(QStringLiteral("显示未修正原始值(&R)"), this);
    m_showRawAction->setCheckable(true);
    m_showRawAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+U")));
    m_showRawAction->setStatusTip(
        QStringLiteral("在“惯导修正后数值”与“未修正原始值”之间切换查看（两者均已存储）"));

    m_showBiasAction = new QAction(QStringLiteral("显示偏差计算(&B)"), this);
    m_showBiasAction->setCheckable(true);
    m_showBiasAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+B")));
    m_showBiasAction->setStatusTip(
        QStringLiteral("在“基础值”与“基础值+手动偏差”之间切换，便于对比偏差效果"));

    m_exitAction = new QAction(QStringLiteral("退出(&Q)"), this);
    m_exitAction->setShortcut(QKeySequence::Quit);

    m_helpAction = new QAction(QStringLiteral("字段说明(&F)"), this);
    m_aboutAction = new QAction(QStringLiteral("关于(&A)"), this);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(QStringLiteral("文件(&F)"));
    fileMenu->addAction(m_openAction);
    fileMenu->addAction(m_reloadAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exportCsvAction);
    fileMenu->addAction(m_exportPngAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_exitAction);

    QMenu *viewMenu = menuBar()->addMenu(QStringLiteral("视图(&V)"));
    viewMenu->addAction(m_resetViewAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_showRawAction);
    viewMenu->addAction(m_showBiasAction);

    QMenu *toolMenu = menuBar()->addMenu(QStringLiteral("工具(&T)"));
    toolMenu->addAction(m_settingsAction);

    QMenu *helpMenu = menuBar()->addMenu(QStringLiteral("帮助(&H)"));
    helpMenu->addAction(m_helpAction);
    helpMenu->addAction(m_aboutAction);
}

void MainWindow::createToolBar()
{
    QToolBar *bar = addToolBar(QStringLiteral("主工具栏"));
    bar->setObjectName(QStringLiteral("mainToolBar"));
    bar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bar->setMovable(false);
    bar->addAction(m_openAction);
    bar->addAction(m_reloadAction);
    bar->addSeparator();
    bar->addAction(m_resetViewAction);
    bar->addAction(m_showRawAction);
    bar->addAction(m_showBiasAction);
    bar->addSeparator();
    bar->addAction(m_exportCsvAction);
    bar->addAction(m_exportPngAction);
    bar->addSeparator();
    bar->addAction(m_settingsAction);
}

void MainWindow::createCentralWidget()
{
    m_splitter = new QSplitter(Qt::Vertical, this);
    m_plot = new PlotWidget(m_splitter);
    m_plot->setMinimumHeight(280);
    m_curvePanel = new CurvePanel(m_splitter);
    // 面板内容（预设行 + 已勾选栏 + 一行分组框）自带滚动区，高度不足时可滚动；
    // 实际的最小高度在 applyContentBasedSplitterSizes() 中按内容计算。
    m_curvePanel->setMinimumHeight(180);
    m_splitter->addWidget(m_plot);
    m_splitter->addWidget(m_curvePanel);
    // 绘图区吸收窗口缩放带来的高度变化；控制面板保持内容所需高度（有下限，必要时内部滚动）
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setSizes(QList<int>() << 560 << 220);

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_splitter);
    setCentralWidget(central);
}

/*!
 * \brief 按控制面板内容所需高度分配分栏高度
 *
 * 面板内容高度随宽度变化（分组框宽度不足时换行，行数越多越高），因此不能沿用固定值：
 * 面板取“内容所需高度”，上限为分栏高度减去绘图区最小高度——即只要能放下，
 * 就保证分组框完整显示（不再出现被裁掉半行、输入框被折叠）。
 */
void MainWindow::applyContentBasedSplitterSizes()
{
    if (!m_splitter || !m_curvePanel || !m_plot)
        return;
    const int total = m_splitter->height();
    if (total < 200)
        return; // 尚未完成首次布局，等窗口显示后再算

    const int width = qMax(320, m_splitter->width());
    const int need = m_curvePanel->preferredHeight(width);
    const int limit = qMax(150, total - m_plot->minimumHeight());
    const int panel = qMin(need, limit);
    m_curvePanel->setMinimumHeight(panel);
    m_splitter->setSizes(QList<int>() << qMax(200, total - panel) << panel);
}

/*!
 * \brief 跟随窗口尺寸同步控制面板高度
 *
 *  - 窗口变窄 → 分组框换行增多、所需高度变大：抬高面板下限，避免分组框被裁掉；
 *  - 窗口变宽 → 所需高度变小：把面板多占的高度还给绘图区（只在面板有多余空间时回收，
 *    不会主动抢占绘图区，也不会覆盖“把面板拖大”的手动调整）。
 */
void MainWindow::syncCurvePanelHeight()
{
    if (!m_splitter || !m_curvePanel || !m_plot)
        return;
    const int total = m_splitter->height();
    if (total < 200)
        return;

    const int need = m_curvePanel->preferredHeight(qMax(320, m_splitter->width()));
    const int limit = qMax(120, total - m_plot->minimumHeight());
    m_curvePanel->setMinimumHeight(qMin(need, limit));
    if (need > limit)
        return; // 窗口太矮：面板内部滚动，不再回收高度

    QList<int> sizes = m_splitter->sizes();
    if (sizes.size() != 2)
        return;
    if (sizes.at(1) > need + 12) {
        sizes[0] += sizes.at(1) - need;
        sizes[1] = need;
        m_splitter->setSizes(sizes);
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (!m_initialLayoutApplied || m_heightSyncPending)
        return;
    // 延后到本轮布局完成后计算：resizeEvent 中分栏尺寸尚未更新，直接算会用到旧宽度
    m_heightSyncPending = true;
    QTimer::singleShot(0, this, [this]() {
        m_heightSyncPending = false;
        syncCurvePanelHeight();
    });
}

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_initialLayoutApplied)
        return;
    // 首次显示时窗口尺寸才最终确定（可能已被窗口管理器/用户调整过），此时按内容分配一次
    m_initialLayoutApplied = true;
    applyContentBasedSplitterSizes();
}

void MainWindow::createStatusBar()
{
    m_fileLabel = new QLabel(QStringLiteral("未加载文件"), this);
    m_configLabel = new QLabel(this);
    m_statsLabel = new QLabel(QStringLiteral("有效帧: 0"), this);
    m_cursorLabel = new QLabel(QStringLiteral("帧序号: -    Y 轴坐标: -"), this);
    m_progress = new QProgressBar(this);
    m_progress->setRange(0, 100);
    m_progress->setFixedWidth(180);
    m_progress->setVisible(false);

    statusBar()->addWidget(m_fileLabel, 1);
    statusBar()->addPermanentWidget(m_progress);
    statusBar()->addPermanentWidget(m_configLabel);
    statusBar()->addPermanentWidget(m_statsLabel);
    statusBar()->addPermanentWidget(m_cursorLabel);
}

void MainWindow::connectSignals()
{
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openFile);
    connect(m_reloadAction, &QAction::triggered, this, &MainWindow::reloadFile);
    connect(m_resetViewAction, &QAction::triggered, m_plot, &PlotWidget::resetView);
    connect(m_exportCsvAction, &QAction::triggered, this, &MainWindow::exportCsv);
    connect(m_exportPngAction, &QAction::triggered, this, &MainWindow::exportPng);
    connect(m_settingsAction, &QAction::triggered, this, &MainWindow::showSettings);
    connect(m_showRawAction, &QAction::toggled, this, &MainWindow::onShowRawToggled);
    connect(m_showBiasAction, &QAction::toggled, this, &MainWindow::onShowBiasToggled);
    connect(m_helpAction, &QAction::triggered, this, &MainWindow::showFieldHelp);
    connect(m_aboutAction, &QAction::triggered, this, &MainWindow::showAbout);
    connect(m_exitAction, &QAction::triggered, this, &QWidget::close);

    connect(m_curvePanel, &CurvePanel::curveToggled, this, &MainWindow::onCurveToggled);
    connect(m_curvePanel, &CurvePanel::fieldBiasChanged, this,
            &MainWindow::onFieldBiasChanged);
    connect(m_curvePanel, &CurvePanel::presetRequested, this, &MainWindow::onPresetRequested);

    connect(m_plot, &PlotWidget::visibilityChanged, this,
            [this](int fieldIndex, bool visible) {
                m_curvePanel->setCheckedSilently(fieldIndex, visible);
            });
    connect(m_plot, &PlotWidget::cursorMoved, this, &MainWindow::onCursorMoved);
    connect(m_plot, &PlotWidget::viewChanged, this, &MainWindow::onPlotViewChanged);
}

void MainWindow::openFile()
{
    const QString dir = AppSettings::lastDirectory();
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开串口 hex 数据文件"), dir,
        QStringLiteral("hex 文本文件 (*.txt *.hex *.dat *.log);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    AppSettings::setLastDirectory(QFileInfo(path).absolutePath());
    parseFile(path);
}

void MainWindow::reloadFile()
{
    reparseCurrent(true);
}

bool MainWindow::parseFile(const QString &path, bool reportErrors)
{
    if (m_busy)
        return false;

    setBusy(true);
    QProgressDialog progress(QStringLiteral("正在解析 %1 ...").arg(QFileInfo(path).fileName()),
                             QStringLiteral("取消"), 0, 100, this);
    progress.setWindowTitle(QStringLiteral("解析中"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setValue(0);
    QApplication::processEvents();

    bool cancelled = false;
    QString error;
    const bool ok = m_manager.load(path, m_config, &error, [&progress, &cancelled](int percent) {
        progress.setValue(percent);
        QApplication::processEvents();
        if (progress.wasCanceled()) {
            cancelled = true;
            return false;
        }
        return true;
    });
    progress.setValue(100);
    progress.close();
    setBusy(false);

    if (!ok) {
        if (reportErrors && !cancelled)
            QMessageBox::critical(this, QStringLiteral("解析失败"), error);
        updateStatus();
        updateActions();
        return false;
    }

    const ParseStats &stats = m_manager.stats();
    if (cancelled) {
        statusBar()->showMessage(QStringLiteral("解析已取消"), 4000);
    } else if (stats.validFrames == 0) {
        QMessageBox::warning(this, QStringLiteral("未识别到有效数据帧"),
                             QStringLiteral("未识别到有效数据帧，请检查文件格式或帧协议配置。"));
    } else if (stats.errorRatio() > 0.10) {
        QMessageBox::warning(
            this, QStringLiteral("错误帧比例偏高"),
            QStringLiteral("错误帧 %1 个，占全部帧的 %2%，请确认帧协议配置或数据完整性。")
                .arg(stats.errorFrames)
                .arg(stats.errorRatio() * 100.0, 0, 'f', 1));
    }

    // 载入绘图控件并重建复选框（沿用用户当前的曲线勾选状态）
    const QVector<bool> visibility = m_plot->visibility();
    m_plot->setSource(&m_manager.data(), &m_manager.config(), visibility);
    m_curvePanel->build(m_manager.config());
    m_curvePanel->beginBatchUpdate();
    for (int f = 0; f < m_manager.config().fields.size(); ++f)
        m_curvePanel->setCheckedSilently(f, m_plot->isCurveVisible(f));
    m_curvePanel->endBatchUpdate();
    // 协议中若已无修正字段，则强制回到修正后显示
    m_showRawAction->setChecked(m_plot->showRawValues());

    // 同步手动偏差到绘图控件与面板（字段数随配置变化，按新长度精确对齐）
    const int newFieldCount = m_manager.config().fields.size();
    m_fieldBias.resize(newFieldCount); // 新增字段补 0、多余字段丢弃
    for (int f = 0; f < newFieldCount; ++f) {
        m_plot->setFieldBias(f, m_fieldBias.at(f));
        m_curvePanel->setBiasSilently(f, m_fieldBias.at(f));
    }
    // 恢复偏差显示开关：setSource 装载阶段已临时关闭偏差，这里切回持久化状态，
    // 开启时会按“基础值+真实偏差”重新装载并自适应 Y；关闭则保持基础值。
    m_plot->setShowBias(m_showBiasAction->isChecked());

    updateStatus();
    updateActions();
    statusBar()->showMessage(QStringLiteral("解析完成"), 3000);
    return true;
}

void MainWindow::reparseCurrent(bool interactive)
{
    if (m_manager.sourcePath().isEmpty()) {
        if (interactive)
            QMessageBox::information(this, QStringLiteral("提示"),
                                     QStringLiteral("尚未加载任何文件。"));
        return;
    }
    // 文件在磁盘上被外部程序修改过时先征求确认，避免在不知情的情况下换成另一份数据
    if (interactive && m_manager.sourceChangedOnDisk()) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("文件已被外部修改"),
            QStringLiteral("磁盘上的文件“%1”已被其它程序修改或替换，是否重新读取？\n"
                           "（选择“否”将继续显示当前已解析的数据）")
                .arg(m_manager.sourceName()));
        if (answer != QMessageBox::Yes)
            return;
    }
    parseFile(m_manager.sourcePath(), interactive);
}

/*!
 * \brief 导出对话框的默认目录
 *
 * 首次运行（未记录上次目录）时使用系统“文档”目录，避免默认路径退化为
 * 进程当前工作目录（可能是只读的安装目录）导致保存失败。
 */
QString MainWindow::defaultSaveDirectory() const
{
    const QString last = AppSettings::lastDirectory();
    if (!last.isEmpty() && QFileInfo(last).isDir())
        return last;
    const QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return documents.isEmpty() ? QDir::homePath() : documents;
}

/*!
 * \brief 带进度对话框的耗时导出
 *
 * \note job 内部捕获的 error 与传入的 error 应为**同一个对象**（失败时用它弹窗）。
 */
bool MainWindow::exportWithProgress(const std::function<bool(const DataManager::ProgressFn &)> &job,
                                    const QString &title, QString *error)
{
    if (m_busy)
        return false;

    setBusy(true);
    QProgressDialog progress(title, QStringLiteral("取消"), 0, 100, this);
    progress.setWindowTitle(QStringLiteral("导出中"));
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);
    progress.setValue(0);
    QApplication::processEvents();

    bool cancelled = false;
    const bool ok = job([&progress, &cancelled](int percent) {
        progress.setValue(percent);
        QApplication::processEvents();
        if (progress.wasCanceled()) {
            cancelled = true;
            return false;
        }
        return true;
    });
    progress.setValue(100);
    progress.close();
    setBusy(false);

    if (!ok) {
        if (cancelled)
            statusBar()->showMessage(QStringLiteral("导出已取消"), 4000);
        else
            QMessageBox::critical(this, QStringLiteral("导出失败"), error ? *error : QString());
        return false;
    }
    return true;
}

void MainWindow::exportCsv()
{
    if (m_manager.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("没有可导出的数据。"));
        return;
    }
    QString base = QFileInfo(m_manager.sourcePath()).completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("turntable_data");
    const QString dir = defaultSaveDirectory();
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出解析结果 CSV"),
        dir + QLatin1Char('/') + base + QStringLiteral(".csv"), QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive))
        path += QStringLiteral(".csv"); // 用户手输扩展名时补齐
    AppSettings::setLastDirectory(QFileInfo(path).absolutePath());

    QString error;
    if (!exportWithProgress(
            [this, &path, &error](const DataManager::ProgressFn &progress) {
                return m_manager.exportCsv(path, &error, progress);
            },
            QStringLiteral("正在导出 CSV %1 ...").arg(QFileInfo(path).fileName()), &error)) {
        return;
    }
    statusBar()->showMessage(QStringLiteral("已导出 %1").arg(path), 5000);
}

void MainWindow::exportPng()
{
    if (!m_plot->hasData()) {
        QMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("没有可导出的图表。"));
        return;
    }
    QString base = QFileInfo(m_manager.sourcePath()).completeBaseName();
    if (base.isEmpty())
        base = QStringLiteral("turntable_plot");
    const QString dir = defaultSaveDirectory();
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("导出图表 PNG"),
        dir + QLatin1Char('/') + base + QStringLiteral(".png"), QStringLiteral("PNG 图片 (*.png)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
        path += QStringLiteral(".png");
    AppSettings::setLastDirectory(QFileInfo(path).absolutePath());

    QString error;
    if (!m_plot->exportPng(path, &error)) {
        QMessageBox::critical(this, QStringLiteral("导出失败"), error);
        return;
    }
    statusBar()->showMessage(QStringLiteral("已导出 %1").arg(path), 5000);
}

void MainWindow::showSettings()
{
    ConfigDialog dialog(m_config, m_plot->displayConfig(), this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const FrameConfig oldConfig = m_config;
    m_config = dialog.frameConfig();
    const DisplayConfig display = dialog.displayConfig();

    AppSettings::saveFrameConfig(m_config);
    AppSettings::saveDisplayConfig(display);
    m_plot->setDisplayConfig(display);
    m_showRawAction->setChecked(m_plot->displayConfig().showRawValues);
    m_showBiasAction->setChecked(m_plot->showBias());

    const bool protocolChanged = oldConfig.frameLength != m_config.frameLength
        || oldConfig.legacyFrameLength != m_config.legacyFrameLength
        || oldConfig.header0 != m_config.header0 || oldConfig.header1 != m_config.header1
        || oldConfig.tail0 != m_config.tail0 || oldConfig.tail1 != m_config.tail1
        || oldConfig.fields.size() != m_config.fields.size();
    bool fieldChanged = protocolChanged;
    if (!fieldChanged) {
        for (int i = 0; i < m_config.fields.size(); ++i) {
            if (oldConfig.fields.at(i) != m_config.fields.at(i)) { // operator!= 覆盖全部成员
                fieldChanged = true;
                break;
            }
        }
    }
    const bool correctChanged = oldConfig.insCorrectEnabled != m_config.insCorrectEnabled
        || oldConfig.correctionMode != m_config.correctionMode
        || oldConfig.axisCorrectEnabled[0] != m_config.axisCorrectEnabled[0]
        || oldConfig.axisCorrectEnabled[1] != m_config.axisCorrectEnabled[1]
        || oldConfig.axisCorrectEnabled[2] != m_config.axisCorrectEnabled[2]
        || oldConfig.gyroAxisForMech[0] != m_config.gyroAxisForMech[0]
        || oldConfig.gyroAxisForMech[1] != m_config.gyroAxisForMech[1]
        || oldConfig.gyroAxisForMech[2] != m_config.gyroAxisForMech[2];

    if ((fieldChanged || correctChanged) && !m_manager.sourcePath().isEmpty()) {
        reparseCurrent(true);
    } else {
        updateStatus();
    }
}

void MainWindow::showFieldHelp()
{
    const FrameConfig &cfg = m_manager.sourcePath().isEmpty() ? m_config : m_manager.config();
    QStringList lines;
    lines << QStringLiteral("<b>帧结构</b>：帧头 %1 %2，帧尾 %3 %4，新帧 %5 字节 / 旧帧 %6 字节，"
                            "大端序；惯导修正来源：%7")
                 .arg(hexByte(cfg.header0), hexByte(cfg.header1), hexByte(cfg.tail0),
                      hexByte(cfg.tail1))
                 .arg(cfg.frameLength)
                 .arg(cfg.legacyFrameLength)
                 .arg(cfg.insCorrectEnabled ? correctionModeName(cfg.correctionMode)
                                            : QStringLiteral("未启用"));
    lines << QStringLiteral("<br><table border='1' cellspacing='0' cellpadding='3'>"
                            "<tr><th align='left'>序号</th><th align='left'>名称</th>"
                            "<th align='left'>变量</th><th align='left'>新帧偏移</th>"
                            "<th align='left'>旧帧偏移</th><th align='left'>类型</th>"
                            "<th align='left'>单位</th><th align='left'>修正</th></tr>");
    for (int i = 0; i < cfg.fields.size(); ++i) {
        const FieldDef &f = cfg.fields.at(i);
        // mechAxis 可能为 -1（不参与修正），必须先判断再索引 gyroAxisForMech
        const bool mechAxisOk = (f.mechAxis >= 0 && f.mechAxis <= 2);
        QString correct = QStringLiteral("—");
        if (f.correct != CorrectSource::None && mechAxisOk) {
            const QString axis = gyroAxisName(cfg.gyroAxisForMech[f.mechAxis]);
            const QString kind = f.correct == CorrectSource::GyroAngle ? QStringLiteral("角度")
                                                                       : QStringLiteral("速度");
            switch (cfg.correctionMode) {
            case CorrectionMode::ImuOnly:
                correct = QStringLiteral("+IMU%1%2").arg(kind, axis);
                break;
            case CorrectionMode::LinsOnly:
                correct = QStringLiteral("+LINS%1%2（旧帧回退 IMU）").arg(kind, axis);
                break;
            case CorrectionMode::ImuAndLins:
                correct = QStringLiteral("+IMU/LINS%1%2").arg(kind, axis);
                break;
            }
        }
        lines << QStringLiteral("<tr><td>%1</td><td><span style='color:%2'>&#9632;</span> %3</td>"
                                "<td>%4</td><td>%5</td><td>%6</td><td>%7</td><td>%8</td>"
                                "<td>%9</td></tr>")
                     .arg(i + 1)
                     .arg(f.color.name(), f.name.toHtmlEscaped(), f.key)
                     .arg(f.offset)
                     .arg(f.legacyOffset < 0 ? QStringLiteral("—") : QString::number(f.legacyOffset))
                     .arg(f.kind == FieldKind::Int16 ? QStringLiteral("int16") : QStringLiteral("uint16"))
                     .arg(f.unit.isEmpty() ? QStringLiteral("-") : f.unit.toHtmlEscaped())
                     .arg(correct);
    }
    lines << QStringLiteral("</table>");

    // 25 行 × 8 列的表格用 QMessageBox 显示会被裁剪，改用可滚动的 QTextBrowser 对话框
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("帧协议字段说明"));
    dialog.resize(820, 560);
    auto *browser = new QTextBrowser(&dialog);
    browser->setHtml(lines.join(QString()));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Close)->setText(QStringLiteral("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(browser, 1);
    layout->addWidget(buttons);
    dialog.exec();
}

void MainWindow::showAbout()
{
    QMessageBox::about(
        this, QStringLiteral("关于"),
        QStringLiteral("<h3>星网转台数据解析与可视化软件 v%1</h3>"
                       "<p>离线解析转台控制单元输出的定长 hex 数据帧，完成 25 个物理量（含 "
                       "IMU5136、LINS358 两套惯导）的解析、惯导角度/速度修正与同坐标系叠加可视化。"
                       "修正来源可在设置中选择 IMU5136 / LINS358 / 两者叠加，默认仅 IMU5136"
                       "（与旧版行为一致）。</p>"
                       "<p>程序可自动识别 54 字节（双惯导）与 42 字节（仅 IMU5136）两种帧长；"
                       "选择仅 LINS358 时，旧帧会自动回退为 IMU5136。</p>"
                       "<p>技术栈：Qt 6.11.0 + QMake + QCustomPlot 2.1.1（C++17 / MinGW 64bit）</p>"
                       "<p>交互：左键框选放大 · 滚轮缩放帧序号轴（Y 轴自适应）· 右键拖拽平移 · "
                       "鼠标悬停查看数值 · 复选框控制曲线显隐。</p>")
            .arg(QLatin1String(kAppVersion)));
}

void MainWindow::onCurveToggled(int fieldIndex, bool checked)
{
    m_plot->setCurveVisible(fieldIndex, checked);
}

void MainWindow::onPresetRequested(int preset)
{
    // 显隐同步由 PlotWidget::visibilityChanged → CurvePanel::setCheckedSilently 完成，
    // 这里无需再遍历一次（否则每个预设都要重复刷新 25 次“已勾选数据”标签）。
    m_plot->applyPreset(static_cast<PlotWidget::Preset>(preset));
}

void MainWindow::onCursorMoved(int frameIndex, double value, bool valid)
{
    if (!valid) {
        m_cursorLabel->setText(QStringLiteral("帧序号: -    Y 轴坐标: -"));
        return;
    }
    // 注意：这里是“光标位置的纵坐标”，不是某条曲线的数值（25 条曲线共用一根 Y 轴），
    // 各曲线的精确数值见绘图区悬停提示框。
    m_cursorLabel->setText(
        QStringLiteral("帧序号: %1    Y 轴坐标: %2").arg(frameIndex).arg(value, 0, 'f', 2));
}

void MainWindow::onShowRawToggled(bool showRaw)
{
    m_plot->setShowRawValues(showRaw);
    if (m_showRawAction->isChecked() != showRaw)
        m_showRawAction->setChecked(showRaw);
    updateStatus();
    statusBar()->showMessage(showRaw ? QStringLiteral("当前显示：未修正原始值")
                                     : QStringLiteral("当前显示：惯导修正后数值"),
                             3000);
}

void MainWindow::onShowBiasToggled(bool show)
{
    m_plot->setShowBias(show);
    if (m_showBiasAction->isChecked() != show)
        m_showBiasAction->setChecked(show);
    updateStatus();
    statusBar()->showMessage(show ? QStringLiteral("当前显示：基础值 + 手动偏差")
                                  : QStringLiteral("当前显示：基础值（未叠加偏差）"),
                             3000);
}

void MainWindow::onFieldBiasChanged(const QVector<int> &fieldIndices, double value)
{
    // 一次更新该行所有字段（同一偏差值），统一保存并刷新一次
    for (int fi : fieldIndices) {
        if (fi < 0 || fi >= m_fieldBias.size())
            continue;
        m_fieldBias[fi] = value;
        m_plot->setFieldBias(fi, value);
    }
    AppSettings::saveFieldBias(m_fieldBias); // 输入后自动保存
    if (m_plot->showBias() && m_plot->hasData())
        m_plot->fitYToVisible();             // 偏差显示开启时立即刷新曲线并自适应 Y
}

void MainWindow::onPlotViewChanged()
{
    updateStatus();
}

void MainWindow::setBusy(bool busy)
{
    m_busy = busy;
    m_progress->setVisible(busy);
    if (busy) {
        QApplication::setOverrideCursor(Qt::WaitCursor);
        m_progress->setValue(0);
    } else {
        QApplication::restoreOverrideCursor();
    }
    updateActions();
    QApplication::processEvents();
}

void MainWindow::updateActions()
{
    const bool hasData = !m_manager.isEmpty();
    m_reloadAction->setEnabled(hasData && !m_busy);
    m_exportCsvAction->setEnabled(hasData && !m_busy);
    m_exportPngAction->setEnabled(hasData && !m_busy);
    m_resetViewAction->setEnabled(hasData && !m_busy);
    m_openAction->setEnabled(!m_busy);
    m_settingsAction->setEnabled(!m_busy);
}

void MainWindow::updateStatus()
{
    if (m_manager.sourcePath().isEmpty()) {
        m_fileLabel->setText(QStringLiteral("未加载文件"));
    } else {
        m_fileLabel->setText(QStringLiteral("文件：%1").arg(m_manager.sourceName()));
    }

    if (m_manager.isEmpty()) {
        m_statsLabel->setText(QStringLiteral("有效帧: 0"));
        m_statsLabel->setToolTip(QString());
    } else {
        const ParseStats &s = m_manager.stats();
        // 注意：errorFrames 的口径是“未被已接受帧覆盖区域内匹配到帧头的次数”，
        // 与残帧字节存在重叠，不能相加当作“总帧数”，因此分开展示。
        m_statsLabel->setText(
            QStringLiteral("有效帧: %1    帧头误匹配: %2    残帧字节: %3    耗时: %4 ms")
                .arg(s.validFrames)
                .arg(s.errorFrames)
                .arg(s.residualBytes)
                .arg(s.elapsedMs, 0, 'f', 1));
        m_statsLabel->setToolTip(m_manager.summaryText()); // 完整统计（含总字节/字段数）
    }

    const FrameConfig &cfg = m_manager.sourcePath().isEmpty() ? m_config : m_manager.config();
    QString valueMode = m_plot && m_plot->showRawValues() ? QStringLiteral("原始值")
                                                          : QStringLiteral("修正后");
    if (m_plot && m_plot->showBias())
        valueMode += QStringLiteral("＋偏差");
    const QString correctText = cfg.insCorrectEnabled
        ? QStringLiteral("开（%1）").arg(correctionModeName(cfg.correctionMode))
        : QStringLiteral("关");
    m_configLabel->setText(
        QStringLiteral("协议: %1 %2 ... %3 %4 / 新帧 %5 / 旧帧 %6 / 惯导修正: %7 / 数值: %8")
            .arg(hexByte(cfg.header0), hexByte(cfg.header1), hexByte(cfg.tail0),
                 hexByte(cfg.tail1))
            .arg(cfg.frameLength)
            .arg(cfg.legacyFrameLength)
            .arg(correctText)
            .arg(valueMode));
}

} // namespace xn
