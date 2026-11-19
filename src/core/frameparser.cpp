#include "frameparser.h"

#include <QElapsedTimer>

namespace xn {

namespace {

/*! \brief 单字段的运行时解析参数（避免逐帧重复字符串查找） */
struct FieldRuntime {
    int offset = 0;        //!< 新帧内字节偏移
    int legacyOffset = -1; //!< 旧帧内字节偏移（-1 表示旧帧无此字段）
    bool isSigned = false; //!< 是否有符号
    double invScale = 0.01;//!< 1 / 换算系数
    int corrIndex = -1;    //!< IMU5136 惯导修正来源字段索引
    int corrIndex2 = -1;   //!< LINS358 惯导修正来源字段索引（与 IMU5136 对等）
    int mechAxis = -1;     // 机械轴索引
    AngleWrap wrap = AngleWrap::None; //!< 角度环绕方式（如横滚角度环绕到 ±180）
};

} // namespace

bool FrameParser::parse(const QByteArray &bytes, const FrameConfig &cfg, DataSet *data,
                        Result *result, QString *error, const FrameParserOptions &options,
                        const ProgressFn &progress)
{
    QElapsedTimer timer;
    timer.start();

    if (result)
        *result = Result();
    if (data)
        data->clear();
    if (error)
        error->clear();

    if (!data || !result) {
        if (error)
            *error = QStringLiteral("内部错误：解析输出参数为空");
        return false;
    }

    QString why;
    if (!cfg.isValid(&why)) {
        if (error)
            *error = QStringLiteral("帧协议配置非法：%1").arg(why);
        return false;
    }

    const int fieldCount = cfg.fields.size();
    const int newLen = cfg.frameLength;       // 新帧长（含 LINS358）
    const int oldLen = cfg.legacyFrameLength; // 旧帧长（仅 IMU5136）
    const qint64 byteCount = bytes.size();
    const uchar *base = reinterpret_cast<const uchar *>(bytes.constData());

    // 预解析字段参数（偏移、类型、换算、惯导修正来源）
    QVector<FieldRuntime> runtime(fieldCount);
    for (int i = 0; i < fieldCount; ++i) {
        const FieldDef &f = cfg.fields.at(i);
        FieldRuntime &r = runtime[i];
        r.offset = f.offset;
        r.legacyOffset = f.legacyOffset;
        r.isSigned = (f.kind == FieldKind::Int16);
        r.invScale = 1.0 / f.scale;
        r.mechAxis = f.mechAxis;
        r.wrap = f.wrap;
        if (f.correct == CorrectSource::None || f.mechAxis < 0 || f.mechAxis > 2)
            continue;
        const int gyroAxis = cfg.gyroAxisForMech[f.mechAxis];
        // 两套惯导对等参与修正：IMU5136 用 corrIndex，LINS358 用 corrIndex2。
        if (f.correct == CorrectSource::GyroAngle) {
            r.corrIndex = cfg.gyroAngleIndex(gyroAxis);   // IMU5136 角度
            r.corrIndex2 = cfg.linsAngleIndex(gyroAxis);  // LINS358 角度
        } else {
            r.corrIndex = cfg.gyroRateIndex(gyroAxis);    // IMU5136 速度
            r.corrIndex2 = cfg.linsRateIndex(gyroAxis);   // LINS358 速度
        }
    }

    // 预留容量（按最小帧长保守估计并设上限，避免超大文件一次性占用过多内存）
    const qint64 estimate = byteCount / qMin(newLen, oldLen);
    data->reserve(int(qMin<qint64>(estimate, 200000)), fieldCount);

    QVector<double> rawValues(fieldCount);
    QVector<double> corrValues(fieldCount);

    const quint8 h0 = cfg.header0;
    const quint8 h1 = cfg.header1;
    const quint8 t0 = cfg.tail0;
    const quint8 t1 = cfg.tail1;

    // 判定 p 处是否为一帧长度为 len 的完整帧：
    //   1) 帧尾两字节匹配；
    //   2) 帧正好到文件末尾（最后一帧），或紧接其后是下一帧帧头（消除帧长歧义）。
    const auto frameFitsAt = [&](qint64 p, int len) -> bool {
        if (p + len > byteCount)
            return false;
        if (base[p + len - 2] != t0 || base[p + len - 1] != t1)
            return false;
        if (p + len == byteCount)
            return true;
        return p + len + 1 < byteCount && base[p + len] == h0 && base[p + len + 1] == h1;
    };

    // 仅校验帧尾（弱匹配）：用于“最后一个完整帧之后还带少量残帧垃圾”的情况，
    // 此时下一帧头位置是垃圾，强匹配会失败，需用弱匹配保住最后一个完整帧。
    const auto tailFitsAt = [&](qint64 p, int len) -> bool {
        return p + len <= byteCount && base[p + len - 2] == t0 && base[p + len - 1] == t1;
    };

    // 某字段在当前帧布局中是否存在（旧帧的 LINS358 字段 legacyOffset=-1）。
    // LINS-only 模式据此决定是否回退到 IMU5136。
    const auto hasField = [&runtime](int fieldIndex, bool isLegacy) -> bool {
        if (fieldIndex < 0)
            return false;
        const FieldRuntime &r = runtime.at(fieldIndex);
        return (isLegacy ? r.legacyOffset : r.offset) >= 0;
    };

    const int minLen = qMin(newLen, oldLen); // 两种帧长都短于它时不可能再容纳完整帧
    qint64 index = 0;
    qint64 validFrames = 0;
    qint64 errorFrames = 0;
    qint64 lastFrameEnd = 0; // 最后一个已接受帧的结束位置（残帧统计基准）
    qint64 nextProgressAt = 0;
    bool cancelled = false;
    int lastPercent = -1;
    // 进度回调步长：小文件也要有足够多的回调次数，否则既看不到进度也无法中途取消
    const qint64 progressStride = qMax<qint64>(1024, byteCount / 300);

    // 剩余长度不足以容纳两种帧长中较短的一种时退出；尾部所有字节计入 residualBytes。
    while (index + minLen <= byteCount) {
        bool frameAccepted = false;
        if (base[index] == h0 && base[index + 1] == h1) {
            // 自动识别帧长：先用强匹配（帧尾 + 到文件末尾或下一帧头），可避免两种帧长
            // 互相误判；强匹配都不满足时（帧尾后带残帧垃圾），退化为仅帧尾的弱匹配。
            bool isNew = false;
            bool isOld = false;
            if (frameFitsAt(index, newLen)) {
                isNew = true;
            } else if (frameFitsAt(index, oldLen)) {
                isOld = true;
            } else if (tailFitsAt(index, newLen)) {
                isNew = true;
            } else if (tailFitsAt(index, oldLen)) {
                isOld = true;
            }
            if (isNew || isOld) {
                const int frameLen = isNew ? newLen : oldLen;
                const bool isLegacy = isOld; // 旧帧用 legacyOffset，新帧用 offset
                const uchar *frame = base + index;
                for (int f = 0; f < fieldCount; ++f) {
                    const FieldRuntime &r = runtime.at(f);
                    const int off = isLegacy ? r.legacyOffset : r.offset;
                    double value = 0.0;
                    // off < 0 表示该字段在当前帧中不存在（如旧帧的 LINS358），取值 0
                    if (off >= 0) {
                        const quint16 u = quint16((quint16(frame[off]) << 8)
                                                  | quint16(frame[off + 1]));
                        value = r.isSigned ? double(qint16(u)) * r.invScale
                                          : double(u) * r.invScale;
                        // 角度环绕：如横滚 0~360 → ±180，超过 180 的部分减 360、显示为负
                        if (r.wrap == AngleWrap::Signed180)
                            value = wrapAngle180(value);
                    }
                    rawValues[f] = value;
                    corrValues[f] = value;
                }
                if (cfg.insCorrectEnabled) {
                    for (int f = 0; f < fieldCount; ++f) {
                        const FieldRuntime &r = runtime.at(f);
                        if (r.mechAxis < 0 || r.mechAxis > 2
                            || !cfg.axisCorrectEnabled[r.mechAxis]
                            || (r.corrIndex < 0 && r.corrIndex2 < 0)) {
                            continue;
                        }
                        const bool imuOk = hasField(r.corrIndex, isLegacy);
                        const bool linsOk = hasField(r.corrIndex2, isLegacy);
                        bool useImu = false;
                        bool useLins = false;
                        switch (cfg.correctionMode) {
                        case CorrectionMode::ImuOnly:
                            useImu = imuOk;
                            break;
                        case CorrectionMode::LinsOnly:
                            // 旧帧（或配置中缺少 LINS358 字段）没有 LINS 时回退到 IMU
                            useLins = linsOk;
                            useImu = !useLins && imuOk;
                            break;
                        case CorrectionMode::ImuAndLins:
                            useImu = imuOk;
                            useLins = linsOk;
                            break;
                        }
                        if (!useImu && !useLins)
                            continue;
                        double v = rawValues[f];
                        if (useImu)
                            v += rawValues.at(r.corrIndex);
                        if (useLins)
                            v += rawValues.at(r.corrIndex2);
                        // 叠加惯导角度后可能再次越过 ±180，按同一约定重新环绕
                        if (r.wrap == AngleWrap::Signed180)
                            v = wrapAngle180(v);
                        corrValues[f] = v;
                    }
                }
                data->appendFrame(rawValues.constData(), corrValues.constData(), fieldCount);
                if (options.collectFrameOffsets)
                    result->frameOffsets.append(qint32(index));
                ++validFrames;
                lastFrameEnd = index + frameLen;
                index += frameLen;
                frameAccepted = true;
            } else {
                ++errorFrames; // 帧头匹配，但两种帧长的帧尾/下一帧头均不匹配
            }
        }
        if (!frameAccepted)
            ++index;

        if (progress && index >= nextProgressAt) {
            nextProgressAt = index + progressStride;
            const int percent = int(index * 100 / byteCount);
            if (percent != lastPercent) {
                lastPercent = percent;
                if (!progress(percent)) {
                    cancelled = true;
                    break;
                }
            }
        }
    }

    ParseStats &stats = result->stats;
    stats.hexBytes = byteCount;
    stats.validFrames = validFrames;
    stats.errorFrames = errorFrames;
    // 残帧口径：最后一个已接受帧之后的全部字节（与 Python 参考实现一致）。
    // 注意不能直接用循环结束时的 index：扫描过程中会被推近文件末尾。
    stats.residualBytes = byteCount - lastFrameEnd;
    stats.fieldCount = fieldCount;
    stats.cancelled = cancelled;
    stats.elapsedMs = double(timer.nsecsElapsed()) / 1e6;
    if (cancelled && error)
        *error = QStringLiteral("解析已取消");
    return true;
}

} // namespace xn
