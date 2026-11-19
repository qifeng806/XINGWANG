#ifndef XN_MAINWINDOW_H
#define XN_MAINWINDOW_H

/*!
 * \file mainwindow.h
 * \brief 主窗口：工具栏、绘图区、曲线控制区、状态栏与全部业务流程信号槽绑定。
 */

#include "appsettings.h"
#include "datamanager.h"
#include "protocol.h"

#include <QMainWindow>

#include <functional>

class QAction;
class QLabel;
class QProgressBar;
class QResizeEvent;
class QSplitter;
class QShowEvent;

namespace xn {

class CurvePanel;
class PlotWidget;

/*! \brief 主窗口 */
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    /*! \brief 直接加载指定文件（供命令行参数调用） */
    bool openPath(const QString &path) { return parseFile(path); }

protected:
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void openFile();
    void reloadFile();
    void exportCsv();
    void exportPng();
    void showSettings();
    void showFieldHelp();
    void showAbout();
    void onCurveToggled(int fieldIndex, bool checked);
    void onPresetRequested(int preset);
    void onCursorMoved(int frameIndex, double value, bool valid);
    void onPlotViewChanged();
    void onShowRawToggled(bool showRaw);
    void onShowBiasToggled(bool show);
    void onFieldBiasChanged(const QVector<int> &fieldIndices, double value);

private:
    void createActions();
    void createMenus();
    void createToolBar();
    void createCentralWidget();
    void createStatusBar();
    void connectSignals();

    /*!
     * \brief 按曲线控制面板的**内容所需高度**分配绘图区/控制区的初始比例
     *
     * 面板底部的分组框会随窗口宽度换行，所需高度是“宽度”的函数，写死分栏比例
     * 必然出现一侧被裁切；这里以内容高度为准，并给绘图区留出足够空间。
     */
    void applyContentBasedSplitterSizes();

    /*! \brief 窗口尺寸变化后同步面板高度（变窄抬高下限、变宽回收多余高度） */
    void syncCurvePanelHeight();

    /*! \brief 解析文件并刷新界面 */
    bool parseFile(const QString &path, bool reportErrors = true);

    /*! \brief 导出对话框的默认目录：上次使用的目录，首次运行时取“文档”目录 */
    QString defaultSaveDirectory() const;

    /*! \brief 带进度对话框且可取消的耗时导出操作（失败/取消时已提示，返回是否成功） */
    bool exportWithProgress(const std::function<bool(const DataManager::ProgressFn &)> &job,
                            const QString &title, QString *error);

    /*! \brief 使用当前配置重新解析已加载文件 */
    void reparseCurrent(bool interactive);

    void updateStatus();
    void updateActions();
    void setBusy(bool busy);

    DataManager m_manager;
    FrameConfig m_config;
    QVector<double> m_fieldBias; //!< 每字段手动偏差（按字段序号，默认 0）
    PlotWidget *m_plot = nullptr;
    CurvePanel *m_curvePanel = nullptr;
    QSplitter *m_splitter = nullptr;

    QLabel *m_fileLabel = nullptr;
    QLabel *m_statsLabel = nullptr;
    QLabel *m_cursorLabel = nullptr;
    QLabel *m_configLabel = nullptr;
    QProgressBar *m_progress = nullptr;

    QAction *m_openAction = nullptr;
    QAction *m_reloadAction = nullptr;
    QAction *m_resetViewAction = nullptr;
    QAction *m_exportCsvAction = nullptr;
    QAction *m_exportPngAction = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_showRawAction = nullptr;
    QAction *m_showBiasAction = nullptr;
    QAction *m_exitAction = nullptr;
    QAction *m_helpAction = nullptr;
    QAction *m_aboutAction = nullptr;

    bool m_busy = false;
    bool m_initialLayoutApplied = false; //!< 首次显示时按内容分配一次分栏高度
    bool m_heightSyncPending = false;    //!< 已排队一次面板高度同步，避免重复排队
};

} // namespace xn

#endif // XN_MAINWINDOW_H
