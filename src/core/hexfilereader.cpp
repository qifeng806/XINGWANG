#include "hexfilereader.h"

#include <QFile>
#include <QFileInfo>

#include <cctype>

namespace xn {

bool HexFileReader::isHexDigit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int HexFileReader::hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

HexReadResult HexFileReader::readFile(const QString &path, const ProgressFn &progress)
{
    HexReadResult result;

    QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        result.error = QStringLiteral("文件不存在：%1").arg(path);
        return result;
    }
    result.sourceBytes = info.size();
    if (result.sourceBytes == 0) {
        result.error = QStringLiteral("文件为空，无法解析");
        return result;
    }
    if (result.sourceBytes > kMaxFileSize) {
        result.error = QStringLiteral("文件大小 %1 MB 超过 100MB 上限")
                           .arg(double(result.sourceBytes) / (1024.0 * 1024.0), 0, 'f', 1);
        return result;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("无法打开文件：%1").arg(file.errorString());
        return result;
    }

    result.bytes.reserve(int(result.sourceBytes / 2 + 16));

    const qint64 chunkSize = 1 << 20; // 1MB
    QByteArray chunk;
    qint64 processed = 0;
    qint64 lastHexOffset = -1; // 最后一个合法 hex 字符的字节偏移（用于奇数 nibble 定位）
    int lastPercent = -1;
    int hiNibble = -1;      // 待拼装的半字节
    bool firstChunk = true;

    while (!file.atEnd()) {
        chunk = file.read(chunkSize);
        if (chunk.isEmpty()) {
            // 区分「正常读完」与「读取出错」：后者必须报错，否则会返回被截断的数据
            if (file.error() != QFileDevice::NoError) {
                result.error = QStringLiteral("读取文件失败：%1").arg(file.errorString());
                file.close();
                return result;
            }
            break;
        }

        int start = 0;
        if (firstChunk && chunk.size() >= 3
            && static_cast<unsigned char>(chunk.at(0)) == 0xEF
            && static_cast<unsigned char>(chunk.at(1)) == 0xBB
            && static_cast<unsigned char>(chunk.at(2)) == 0xBF) {
            start = 3; // 跳过 UTF-8 BOM
        }
        firstChunk = false;

        for (int i = start; i < chunk.size(); ++i) {
            const char c = chunk.at(i);
            const unsigned char uc = static_cast<unsigned char>(c);
            if (std::isspace(uc))
                continue;
            const int v = hexValue(c);
            if (v < 0) {
                const QString hex = QString::number(uc, 16).toUpper().rightJustified(2, QLatin1Char('0'));
                result.error = QStringLiteral("文件字节偏移 %1 处存在非法 hex 字符（0x%2），"
                                              "文件中仅允许出现 hex 字符与空白字符")
                                   .arg(processed + i)
                                   .arg(hex);
                result.errorOffset = processed + i;
                file.close();
                return result;
            }
            if (hiNibble < 0) {
                hiNibble = v;
            } else {
                result.bytes.append(char((hiNibble << 4) | v));
                hiNibble = -1;
            }
            lastHexOffset = processed + i;
            ++result.charCount;
        }

        processed += chunk.size();
        if (progress) {
            const int percent = int(processed * 100 / result.sourceBytes);
            if (percent != lastPercent) {
                lastPercent = percent;
                if (!progress(percent)) {
                    result.cancelled = true;
                    result.error = QStringLiteral("解析已取消");
                    file.close();
                    return result;
                }
            }
        }
    }
    file.close();

    if (hiNibble >= 0) {
        result.error = QStringLiteral("hex 字符总数为奇数，文件末尾（字节偏移 %1）存在不完整的半个字节")
                           .arg(lastHexOffset >= 0 ? lastHexOffset : processed);
        result.errorOffset = lastHexOffset >= 0 ? lastHexOffset : processed;
        return result;
    }
    if (result.bytes.isEmpty()) {
        result.error = QStringLiteral("文件中未找到任何 hex 数据");
        return result;
    }

    result.ok = true;
    return result;
}

} // namespace xn
