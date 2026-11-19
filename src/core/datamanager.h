#ifndef XN_DATAMANAGER_H
#define XN_DATAMANAGER_H

/*!
 * \file datamanager.h
 * \brief 数据管理：串联文件读取、帧解析、结果存储与 CSV 导出。
 */

#include "framedata.h"
#include "protocol.h"

#include <QDateTime>
#include <QObject>
#include <QString>

#include <functional>

namespace xn {

/*! \brief 数据管理器 */
class DataManager : public QObject {
    Q_OBJECT
public:
    /*! \brief 进度回调：percent 为 0~100，返回 false 表示用户中断 */
    using ProgressFn = std::function<bool(int percent)>;

    explicit DataManager(QObject *parent = nullptr);

    /*! \brief 读取并解析文件（失败时通过 error 返回原因） */
    bool load(const QString &path, const FrameConfig &cfg, QString *error,
              const ProgressFn &progress = ProgressFn());

    /*! \brief 仅重新解析已加载的文件（使用新配置） */
    bool reload(const FrameConfig &cfg, QString *error, const ProgressFn &progress = ProgressFn());

    /*! \brief 源文件在磁盘上是否已被外部修改（用于重新解析前提示用户） */
    bool sourceChangedOnDisk() const;

    void clear();

    const DataSet &data() const { return m_data; }
    const ParseStats &stats() const { return m_stats; }
    const FrameConfig &config() const { return m_config; }
    const QString &sourcePath() const { return m_sourcePath; }
    QString sourceName() const;
    bool isEmpty() const { return m_data.isEmpty(); }

    /*! \brief 导出解析结果 CSV（UTF-8 带 BOM：帧序号 + 原始值 + 修正值）
     *  \param progress 进度回调（percent 0~100，返回 false 表示用户取消导出） */
    bool exportCsv(const QString &path, QString *error,
                   const ProgressFn &progress = ProgressFn()) const;

    /*! \brief CSV 表头（无 BOM 前缀） */
    QString csvHeader() const;

    /*! \brief 解析结果的汇总文本（状态栏/提示用） */
    QString summaryText() const;

signals:
    /*! \brief 数据装载完成 */
    void dataLoaded();

private:
    DataSet m_data;
    ParseStats m_stats;
    FrameConfig m_config;
    QString m_sourcePath;
    QDateTime m_sourceModified; //!< 上次读取时源文件的修改时间
};

} // namespace xn

#endif // XN_DATAMANAGER_H
