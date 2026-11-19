#ifndef XN_HEXFILEREADER_H
#define XN_HEXFILEREADER_H

/*!
 * \file hexfilereader.h
 * \brief hex 文本文件读取：白字符过滤、字符合法性校验、分块读取与进度反馈。
 */

#include <QByteArray>
#include <QString>

#include <functional>

namespace xn {

/*! \brief 读取结果 */
struct HexReadResult {
    bool ok = false;             //!< 是否读取成功
    bool cancelled = false;      //!< 是否被用户中断
    QByteArray bytes;            //!< 解析出的原始字节流
    QString error;               //!< 错误描述（ok=false 时有效）
    qint64 errorOffset = -1;     //!< 非法字符所在偏移（源文件字符序号，0 起）
    qint64 sourceBytes = 0;      //!< 源文件字节数
    qint64 charCount = 0;        //!< 有效 hex 字符数（已过滤白字符）
};

/*!
 * \brief hex 文本读取器
 *
 * 支持 ANSI / UTF-8 编码的 txt（hex 文本均为 ASCII，按字节读取即可）；
 * 自动过滤空格、制表符、回车、换行等白字符；分块读取并通过回调反馈进度。
 */
class HexFileReader {
public:
    /*! \brief 进度回调：percent 为 0~100，返回 false 表示用户中断 */
    using ProgressFn = std::function<bool(int percent)>;

    /*! \brief 单文件大小上限：100MB */
    static constexpr qint64 kMaxFileSize = 100LL * 1024 * 1024;

    /*! \brief 读取文件并转换为字节流 */
    static HexReadResult readFile(const QString &path, const ProgressFn &progress = ProgressFn());

    /*! \brief 是否为合法 hex 字符 */
    static bool isHexDigit(char c);

    /*! \brief hex 字符转半字节，非法字符返回 -1 */
    static int hexValue(char c);
};

} // namespace xn

#endif // XN_HEXFILEREADER_H
