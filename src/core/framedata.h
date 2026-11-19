#ifndef XN_FRAMEDATA_H
#define XN_FRAMEDATA_H

/*!
 * \file framedata.h
 * \brief 解析统计信息与解析结果数据集（连续内存存储，O(1) 随机访问）。
 */

#include <QVector>

namespace xn {

/*! \brief 解析统计信息 */
struct ParseStats {
    qint64 sourceBytes = 0;   //!< 源 txt 文件字节数
    qint64 hexBytes = 0;      //!< 过滤白字符后得到的纯 hex 字节数
    qint64 validFrames = 0;   //!< 有效帧数（帧头帧尾均校验通过）
    qint64 errorFrames = 0;   //!< 错误帧数（匹配到帧头但帧尾校验失败）
    qint64 residualBytes = 0; //!< 残帧字节数（文件尾部不足一帧）
    qint64 fieldCount = 0;    //!< 每帧字段数
    double elapsedMs = 0.0;   //!< 解析耗时（毫秒）
    bool cancelled = false;   //!< 是否被用户中断

    /*! \brief 错误帧占比（错误帧 /（有效帧 + 错误帧）） */
    double errorRatio() const;

    void reset();
};

/*!
 * \brief 解析结果数据集
 *
 * 数据按帧连续存储（行主序），既保留未修正的原始物理量，也保留惯导修正后的值，
 * 便于对比查看。内存占用约为 帧数 × 字段数 × 16 字节。
 */
class DataSet {
public:
    void clear();

    /*! \brief 预留容量，避免逐帧追加时反复扩容 */
    void reserve(int frameCount, int fieldCount);

    /*! \brief 追加一帧（raw/corrected 均为 fieldCount 个 double） */
    void appendFrame(const double *raw, const double *corrected, int count);

    int frameCount() const { return m_frameCount; }
    int fieldCount() const { return m_fieldCount; }
    bool isEmpty() const { return m_frameCount == 0; }

    /*! \brief 原始（未修正）值 */
    double rawValue(int frame, int field) const;

    /*! \brief 修正后的值（未启用修正时等于原始值） */
    double value(int frame, int field) const;

    /*! \brief 某帧原始值数组首地址（长度 fieldCount） */
    const double *rawFrame(int frame) const;

    /*! \brief 某帧修正值数组首地址（长度 fieldCount） */
    const double *frame(int frame) const;

    /*! \brief 抽取某字段整列数据
     *  \warning 返回**整列**副本（帧数 × 8 字节）；百万帧级数据请勿在 UI 路径调用，
     *           绘图请使用 PlotWidget 的窗口化抽取（DisplayConfig::maxPointsPerCurve）。 */
    QVector<double> column(int field, bool corrected) const;

    /*! \brief 某字段整列之和（用于自检/统计）
     *  \warning O(帧数) 复杂度且不做抽稀，百万帧级数据请勿在 UI 事件处理中同步调用。 */
    double columnSum(int field, bool corrected) const;

    /*! \brief 近似内存占用（字节） */
    qint64 memoryUsage() const;

private:
    QVector<double> m_raw;     //!< 原始值，行主序
    QVector<double> m_corr;    //!< 修正值，行主序
    int m_frameCount = 0;
    int m_fieldCount = 0;
};

} // namespace xn

#endif // XN_FRAMEDATA_H
