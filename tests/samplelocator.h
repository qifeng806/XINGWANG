#ifndef XW_TESTS_SAMPLELOCATOR_H
#define XW_TESTS_SAMPLELOCATOR_H

/*!
 * \file samplelocator.h
 * \brief 测试用样例数据定位（供 test_parser.cpp / test_ui.cpp 共用）
 *
 * 查找顺序：
 *   1) 环境变量 XW_SAMPLE（54 字节样例）/ XW_SAMPLE_LEGACY（42 字节样例）指定的文件；
 *   2) 从「可执行文件所在目录」开始逐级向上查找 sample/<固定文件名>
 *      （兼容任意影子构建层级，例如 build/bin/release、build-tests/core/bin/release 等）；
 *   3) 从「当前工作目录」开始同样逐级向上查找。
 *
 * 举例说明：样例文件按名称显式定位，避免 sample/ 下同时存在 42/54 字节数据时
 * 因字母序变化而误用另一份数据，导致统计/数值断言全部失效。
 */

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace xwtest {

inline QString findNamedSampleInAncestors(const QString &startDir, const QString &fileName,
                                          int maxDepth = 8)
{
    QDir dir(startDir);
    for (int depth = 0; depth < maxDepth; ++depth) {
        const QFileInfo candidate(
            dir.absoluteFilePath(QStringLiteral("sample/%1").arg(fileName)));
        if (candidate.isFile())
            return candidate.absoluteFilePath();
        if (!dir.cdUp())
            break;
    }
    return QString();
}

inline QString locateNamedSample(const char *envName, const QString &fileName)
{
    const QByteArray env = qgetenv(envName);
    if (!env.isEmpty() && QFileInfo::exists(QString::fromLocal8Bit(env)))
        return QString::fromLocal8Bit(env);

    const QString fromExe =
        findNamedSampleInAncestors(QCoreApplication::applicationDirPath(), fileName);
    if (!fromExe.isEmpty())
        return fromExe;
    return findNamedSampleInAncestors(QDir::currentPath(), fileName);
}

/*! \brief 定位 54 字节双惯导样例（找不到时返回空串，调用方应 QSKIP） */
inline QString locateDualSampleFile()
{
    return locateNamedSample("XW_SAMPLE", QStringLiteral("20261010_54byte_3841frames.txt"));
}

/*! \brief 定位 42 字节旧帧样例（LINS358 缺失） */
inline QString locateLegacySampleFile()
{
    return locateNamedSample("XW_SAMPLE_LEGACY",
                             QStringLiteral("20260923_42byte_1304frames.txt"));
}

/*! \brief 兼容旧调用：默认样例为当前的 54 字节双惯导数据 */
inline QString locateSampleFile()
{
    return locateDualSampleFile();
}

} // namespace xwtest

#endif // XW_TESTS_SAMPLELOCATOR_H
