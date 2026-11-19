#include "framedata.h"

#include <algorithm>

namespace xn {

double ParseStats::errorRatio() const
{
    const qint64 total = validFrames + errorFrames;
    if (total <= 0)
        return 0.0;
    return double(errorFrames) / double(total);
}

void ParseStats::reset()
{
    *this = ParseStats();
}

void DataSet::clear()
{
    m_raw.clear();
    m_corr.clear();
    m_frameCount = 0;
    m_fieldCount = 0;
}

void DataSet::reserve(int frameCount, int fieldCount)
{
    if (frameCount <= 0 || fieldCount <= 0)
        return;
    // 在已有数据上以不同的字段数 reserve 会让后续索引全部错位，必须重置
    if (m_frameCount > 0 && fieldCount != m_fieldCount)
        clear();
    m_fieldCount = fieldCount;
    const qsizetype total = qsizetype(frameCount) * qsizetype(fieldCount);
    m_raw.reserve(total);
    m_corr.reserve(total);
}

void DataSet::appendFrame(const double *raw, const double *corrected, int count)
{
    if (count <= 0)
        return;
    if (m_fieldCount != count) {
        // 字段数变化（重新配置协议后重新解析）：重置数据集
        clear();
        m_fieldCount = count;
    }
    const qsizetype oldSize = m_raw.size();
    m_raw.resize(oldSize + count);
    m_corr.resize(oldSize + count);
    std::copy(raw, raw + count, m_raw.begin() + oldSize);
    std::copy(corrected, corrected + count, m_corr.begin() + oldSize);
    ++m_frameCount;
}

double DataSet::rawValue(int frame, int field) const
{
    if (frame < 0 || frame >= m_frameCount || field < 0 || field >= m_fieldCount)
        return 0.0;
    return m_raw.at(qsizetype(frame) * m_fieldCount + field);
}

double DataSet::value(int frame, int field) const
{
    if (frame < 0 || frame >= m_frameCount || field < 0 || field >= m_fieldCount)
        return 0.0;
    return m_corr.at(qsizetype(frame) * m_fieldCount + field);
}

const double *DataSet::rawFrame(int frame) const
{
    if (frame < 0 || frame >= m_frameCount)
        return nullptr;
    return m_raw.constData() + qsizetype(frame) * m_fieldCount;
}

const double *DataSet::frame(int frame) const
{
    if (frame < 0 || frame >= m_frameCount)
        return nullptr;
    return m_corr.constData() + qsizetype(frame) * m_fieldCount;
}

QVector<double> DataSet::column(int field, bool corrected) const
{
    QVector<double> out;
    if (field < 0 || field >= m_fieldCount)
        return out;
    out.resize(m_frameCount);
    const QVector<double> &src = corrected ? m_corr : m_raw;
    for (int i = 0; i < m_frameCount; ++i)
        out[i] = src.at(qsizetype(i) * m_fieldCount + field);
    return out;
}

double DataSet::columnSum(int field, bool corrected) const
{
    if (field < 0 || field >= m_fieldCount)
        return 0.0;
    const QVector<double> &src = corrected ? m_corr : m_raw;
    double sum = 0.0;
    for (int i = 0; i < m_frameCount; ++i)
        sum += src.at(qsizetype(i) * m_fieldCount + field);
    return sum;
}

qint64 DataSet::memoryUsage() const
{
    return qint64(m_raw.size() + m_corr.size()) * qint64(sizeof(double));
}

} // namespace xn
