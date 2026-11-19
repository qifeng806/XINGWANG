#include "datamanager.h"

#include "frameparser.h"
#include "hexfilereader.h"

#include <QFileInfo>
#include <QSaveFile>

namespace xn {

DataManager::DataManager(QObject *parent)
    : QObject(parent)
{
    m_config = FrameConfig::defaultConfig();
}

bool DataManager::load(const QString &path, const FrameConfig &cfg, QString *error,
                       const ProgressFn &progress)
{
    if (error)
        error->clear();

    QString why;
    if (!cfg.isValid(&why)) {
        if (error)
            *error = QStringLiteral("帧协议配置非法：%1").arg(why);
        return false;
    }

    // 1) 读取 hex 文本（读取阶段占 0~45% 进度）
    const HexReadResult read = HexFileReader::readFile(path, [&progress](int percent) {
        return progress ? progress(percent * 45 / 100) : true;
    });
    if (!read.ok) {
        if (error)
            *error = read.error;
        return false;
    }

    // 2) 帧同步与字段解析（解析阶段占 45~100% 进度）
    DataSet data;
    FrameParser::Result result;
    const bool ok = FrameParser::parse(read.bytes, cfg, &data, &result, error,
                                       FrameParserOptions(), [&progress](int percent) {
                                           return progress ? progress(45 + percent * 55 / 100) : true;
                                       });
    if (!ok)
        return false;

    // 用户中断时解析结果是“半截数据”，直接丢弃，避免界面上显示不完整的曲线，
    // 同时保留上一次成功加载的数据。
    if (result.stats.cancelled) {
        if (error)
            *error = QStringLiteral("解析已取消");
        return false;
    }

    result.stats.sourceBytes = read.sourceBytes;
    m_data = data;
    m_stats = result.stats;
    m_config = cfg;
    m_sourcePath = path;
    m_sourceModified = QFileInfo(path).lastModified();
    emit dataLoaded();
    return true;
}

bool DataManager::reload(const FrameConfig &cfg, QString *error, const ProgressFn &progress)
{
    if (m_sourcePath.isEmpty()) {
        if (error)
            *error = QStringLiteral("尚未加载任何文件");
        return false;
    }
    return load(m_sourcePath, cfg, error, progress);
}

bool DataManager::sourceChangedOnDisk() const
{
    if (m_sourcePath.isEmpty() || !m_sourceModified.isValid())
        return false;
    const QFileInfo info(m_sourcePath);
    if (!info.exists())
        return true;
    return info.lastModified() != m_sourceModified;
}

void DataManager::clear()
{
    m_data.clear();
    m_stats.reset();
    m_sourcePath.clear();
}

QString DataManager::sourceName() const
{
    if (m_sourcePath.isEmpty())
        return QString();
    return QFileInfo(m_sourcePath).fileName();
}

QString DataManager::csvHeader() const
{
    QStringList columns;
    columns << QStringLiteral("帧序号");
    for (const FieldDef &f : m_config.fields) {
        const QString unit = f.unit.isEmpty() ? QString() : QStringLiteral("(%1)").arg(f.unit);
        columns << QStringLiteral("%1%2").arg(f.name, unit);
    }
    for (const FieldDef &f : m_config.fields) {
        if (f.correct == CorrectSource::None)
            continue;
        const QString unit = f.unit.isEmpty() ? QString() : QStringLiteral("(%1)").arg(f.unit);
        columns << QStringLiteral("%1(修正)%2").arg(f.name, unit);
    }
    return columns.join(QLatin1Char(','));
}

bool DataManager::exportCsv(const QString &path, QString *error, const ProgressFn &progress) const
{
    if (error)
        error->clear();
    if (m_data.isEmpty()) {
        if (error)
            *error = QStringLiteral("没有可导出的数据，请先解析文件");
        return false;
    }
    // 表头由 m_config.fields 生成、数据行由 m_data 生成，两者必须一致，
    // 否则会导出“表头列数 ≠ 数据列数”的错位 CSV（静默数据损坏）。
    if (m_config.fields.size() != m_data.fieldCount()) {
        if (error)
            *error = QStringLiteral("内部状态不一致：协议字段数 %1 与数据列数 %2 不匹配，"
                                    "请重新解析文件")
                         .arg(m_config.fields.size())
                         .arg(m_data.fieldCount());
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入文件：%1").arg(file.errorString());
        return false;
    }

    const int fields = m_config.fields.size();
    const auto writeAll = [&file](const QByteArray &data) {
        return data.isEmpty() || file.write(data) == data.size();
    };
    if (!writeAll(QByteArray("\xEF\xBB\xBF"))   // UTF-8 BOM，保证 Excel 正确识别中文
        || !writeAll(csvHeader().toUtf8()) || !writeAll(QByteArray("\r\n"))) {
        if (error)
            *error = QStringLiteral("写入文件失败：%1").arg(file.errorString());
        file.cancelWriting();
        return false;
    }

    // 参与修正的字段索引
    QVector<int> correctedFields;
    for (int f = 0; f < fields; ++f) {
        if (m_config.fields.at(f).correct != CorrectSource::None)
            correctedFields.append(f);
    }

    const int frameCount = m_data.frameCount();
    QByteArray buffer;
    buffer.reserve(1 << 20);
    int lastPercent = -1;
    for (int frame = 0; frame < frameCount; ++frame) {
        const double *raw = m_data.rawFrame(frame);
        const double *corr = m_data.frame(frame);
        buffer += QByteArray::number(frame);
        // 业务要求：CSV 固定导出两位小数（不是精度缺陷，请勿改为更高精度）。
        for (int f = 0; f < fields; ++f) {
            buffer += ',';
            buffer += QByteArray::number(raw[f], 'f', 2);
        }
        for (int f : correctedFields) {
            buffer += ',';
            buffer += QByteArray::number(corr[f], 'f', 2);
        }
        buffer += "\r\n";
        if (buffer.size() >= (1 << 20)) {
            if (file.write(buffer) != buffer.size()) {
                if (error)
                    *error = QStringLiteral("写入文件失败：%1").arg(file.errorString());
                file.cancelWriting();
                return false;
            }
            buffer.clear();
            // 大数据量导出耗时可观，按百分比回调以便界面显示进度并支持取消
            if (progress) {
                const int percent = int(qint64(frame) * 100 / qMax(1, frameCount));
                if (percent != lastPercent) {
                    lastPercent = percent;
                    if (!progress(percent)) {
                        if (error)
                            *error = QStringLiteral("导出已取消");
                        file.cancelWriting();
                        return false;
                    }
                }
            }
        }
    }
    if (!buffer.isEmpty() && file.write(buffer) != buffer.size()) {
        if (error)
            *error = QStringLiteral("写入文件失败：%1").arg(file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error)
            *error = QStringLiteral("保存文件失败：%1").arg(file.errorString());
        return false;
    }
    return true;
}

QString DataManager::summaryText() const
{
    return QStringLiteral("总字节 %1 | 有效帧 %2 | 错误帧 %3 | 残帧字节 %4 | 字段 %5 | 耗时 %6 ms")
        .arg(m_stats.hexBytes)
        .arg(m_stats.validFrames)
        .arg(m_stats.errorFrames)
        .arg(m_stats.residualBytes)
        .arg(m_stats.fieldCount)
        .arg(m_stats.elapsedMs, 0, 'f', 1);
}

} // namespace xn
