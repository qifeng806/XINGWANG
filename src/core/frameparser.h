#ifndef XN_FRAMEPARSER_H
#define XN_FRAMEPARSER_H

/*!
 * \file frameparser.h
 * \brief 帧同步、帧尾校验、字段解析与惯导修正（滑动窗口，复杂度 O(n)）。
 */

#include "framedata.h"
#include "protocol.h"

#include <QByteArray>
#include <QString>
#include <QVector>

#include <functional>

namespace xn {

/*! \brief 解析选项 */
struct FrameParserOptions {
    bool collectFrameOffsets = false; //!< 是否记录每帧起始字节偏移（诊断用）
};

/*! \brief 帧解析器 */
class FrameParser {
public:
    /*! \brief 进度回调：percent 为 0~100，返回 false 表示用户中断 */
    using ProgressFn = std::function<bool(int percent)>;

    /*! \brief 解析结果（统计信息 + 可选的帧偏移表） */
    struct Result {
        ParseStats stats;
        QVector<qint32> frameOffsets;
    };

    /*!
     * \brief 解析字节流
     * \param bytes    hex 文本转换后的原始字节流
     * \param cfg      帧协议配置
     * \param data     输出数据集（**不可为空**；只需统计信息时请传入临时 DataSet）
     * \param result   输出统计结果
     * \param error    失败原因
     * \param options  解析选项
     * \param progress 进度回调
     * \return 是否解析成功（未识别到有效帧不算失败，通过 stats 判断）
     */
    static bool parse(const QByteArray &bytes, const FrameConfig &cfg, DataSet *data,
                      Result *result, QString *error,
                      const FrameParserOptions &options = FrameParserOptions(),
                      const ProgressFn &progress = ProgressFn());
};

} // namespace xn

#endif // XN_FRAMEPARSER_H
