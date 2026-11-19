#include "protocol.h"

#include <QSet>

#include <cmath>

namespace xn {

namespace {

/*! \brief 25 条曲线的默认配色：角度暖色 / 速度冷色 / IMU5136 棕灰 / LINS358 绿青 / 信标洋红 */
const QColor kPalette[25] = {
    QColor(0xE5, 0x39, 0x35), // 方位目标角度   红
    QColor(0xD8, 0x1B, 0x60), // 方位编码器角度 玫红
    QColor(0x1E, 0x88, 0xE5), // 方位运行速度   蓝
    QColor(0x26, 0xA6, 0x9A), // 方位前馈速度   青绿
    QColor(0xFB, 0x8C, 0x00), // 俯仰目标角度   橙
    QColor(0xFF, 0x70, 0x43), // 俯仰编码器角度 深橙
    QColor(0x00, 0xAC, 0xC1), // 俯仰运行速度   青
    QColor(0x5E, 0x35, 0xB1), // 俯仰前馈速度   紫
    QColor(0xFD, 0xD8, 0x35), // 横滚目标角度   黄
    QColor(0xFF, 0xB3, 0x00), // 横滚编码器角度 琥珀
    QColor(0x39, 0x49, 0xAB), // 横滚运行速度   靛蓝
    QColor(0x02, 0x88, 0xD1), // 横滚前馈速度   深天蓝
    QColor(0x6D, 0x4C, 0x41), // IMU5136 角度X 棕
    QColor(0x75, 0x75, 0x75), // IMU5136 角度Y 灰
    QColor(0x45, 0x5A, 0x64), // IMU5136 角度Z 蓝灰
    QColor(0x8D, 0x6E, 0x63), // IMU5136 速度X 浅棕
    QColor(0x9E, 0x9E, 0x9E), // IMU5136 速度Y 浅灰
    QColor(0x78, 0x90, 0x9C), // IMU5136 速度Z 浅蓝灰
    QColor(0x2E, 0x7D, 0x32), // LINS358 角度X 深绿
    QColor(0x00, 0x83, 0x8F), // LINS358 角度Y 深青
    QColor(0x15, 0x65, 0xC0), // LINS358 角度Z 深蓝
    QColor(0x66, 0xBB, 0x6A), // LINS358 速度X 浅绿
    QColor(0x26, 0xC6, 0xDA), // LINS358 速度Y 浅青
    QColor(0x42, 0xA5, 0xF5), // LINS358 速度Z 浅蓝
    QColor(0xD5, 0x00, 0xF9)  // 信标功率值     洋红（高亮）
};

FieldDef makeField(const QString &key, const QString &name, FieldGroup group,
                   FieldKind kind, const QString &unit, int offset,
                   CorrectSource correct, int mechAxis, QColor color,
                   AngleWrap wrap = AngleWrap::None, int legacyOffset = -2)
{
    FieldDef f;
    f.key = key;
    f.name = name;
    f.group = group;
    f.kind = kind;
    f.unit = unit;
    f.offset = offset;
    // legacyOffset == -2（哨兵）表示旧帧偏移与新帧相同；
    // -1 表示旧帧无此字段；其余为显式旧帧偏移。
    f.legacyOffset = (legacyOffset == -2) ? offset : legacyOffset;
    f.correct = correct;
    f.mechAxis = mechAxis;
    f.color = color;
    f.scale = 100.0;
    f.wrap = wrap;
    return f;
}

} // namespace

FrameConfig FrameConfig::defaultConfig()
{
    FrameConfig cfg;
    QVector<FieldDef> &f = cfg.fields;
    f.reserve(25);

    //                       变量名          中文名称          分组             类型               单位   偏移  修正来源                机械轴
    f << makeField("AZ_TARGET",   QStringLiteral("方位目标角度"),   FieldGroup::Angle,  FieldKind::UInt16, QStringLiteral("°"),   2,  CorrectSource::GyroAngle, MechAzimuth,   defaultFieldColor(0));
    f << makeField("AZ_ENCODER",  QStringLiteral("方位编码器角度"), FieldGroup::Angle,  FieldKind::UInt16, QStringLiteral("°"),   4,  CorrectSource::None,      -1,            defaultFieldColor(1));
    f << makeField("AZ_SPEED",    QStringLiteral("方位运行速度"),   FieldGroup::Rate,   FieldKind::Int16,  QStringLiteral("°/s"), 6,  CorrectSource::None,      -1,            defaultFieldColor(2));
    f << makeField("AZ_FF",       QStringLiteral("方位前馈速度"),   FieldGroup::Rate,   FieldKind::Int16,  QStringLiteral("°/s"), 8,  CorrectSource::GyroRate,  MechAzimuth,   defaultFieldColor(3));
    f << makeField("EL_TARGET",   QStringLiteral("俯仰目标角度"),   FieldGroup::Angle,  FieldKind::UInt16, QStringLiteral("°"),   10, CorrectSource::GyroAngle, MechElevation, defaultFieldColor(4));
    f << makeField("EL_ENCODER",  QStringLiteral("俯仰编码器角度"), FieldGroup::Angle,  FieldKind::UInt16, QStringLiteral("°"),   12, CorrectSource::None,      -1,            defaultFieldColor(5));
    f << makeField("EL_SPEED",    QStringLiteral("俯仰运行速度"),   FieldGroup::Rate,   FieldKind::Int16,  QStringLiteral("°/s"), 14, CorrectSource::None,      -1,            defaultFieldColor(6));
    f << makeField("EL_FF",       QStringLiteral("俯仰前馈速度"),   FieldGroup::Rate,   FieldKind::Int16,  QStringLiteral("°/s"), 16, CorrectSource::GyroRate,  MechElevation, defaultFieldColor(7));
    f << makeField("RL_TARGET",   QStringLiteral("横滚目标角度"),   FieldGroup::Angle,  FieldKind::UInt16, QStringLiteral("°"),   18, CorrectSource::GyroAngle, MechRoll,      defaultFieldColor(8), AngleWrap::Signed180);
    f << makeField("RL_ENCODER",  QStringLiteral("横滚编码器角度"), FieldGroup::Angle,  FieldKind::UInt16, QStringLiteral("°"),   20, CorrectSource::None,      -1,            defaultFieldColor(9), AngleWrap::Signed180);
    f << makeField("RL_SPEED",    QStringLiteral("横滚运行速度"),   FieldGroup::Rate,   FieldKind::Int16,  QStringLiteral("°/s"), 22, CorrectSource::None,      -1,            defaultFieldColor(10));
    f << makeField("RL_FF",       QStringLiteral("横滚前馈速度"),   FieldGroup::Rate,   FieldKind::Int16,  QStringLiteral("°/s"), 24, CorrectSource::GyroRate,  MechRoll,      defaultFieldColor(11));
    // ── IMU5136 惯导（参与控制修正，名称加型号前缀以便与 LINS358 区分） ──
    f << makeField("GYRO_ANG_X",  QStringLiteral("IMU5136角度X"),   FieldGroup::Gyro,   FieldKind::Int16,  QStringLiteral("°"),   26, CorrectSource::None,      -1,            defaultFieldColor(12));
    f << makeField("GYRO_ANG_Y",  QStringLiteral("IMU5136角度Y"),   FieldGroup::Gyro,   FieldKind::Int16,  QStringLiteral("°"),   28, CorrectSource::None,      -1,            defaultFieldColor(13));
    f << makeField("GYRO_ANG_Z",  QStringLiteral("IMU5136角度Z"),   FieldGroup::Gyro,   FieldKind::Int16,  QStringLiteral("°"),   30, CorrectSource::None,      -1,            defaultFieldColor(14));
    f << makeField("GYRO_RATE_X", QStringLiteral("IMU5136速度X"),   FieldGroup::Gyro,   FieldKind::Int16,  QStringLiteral("°/s"), 32, CorrectSource::None,      -1,            defaultFieldColor(15));
    f << makeField("GYRO_RATE_Y", QStringLiteral("IMU5136速度Y"),   FieldGroup::Gyro,   FieldKind::Int16,  QStringLiteral("°/s"), 34, CorrectSource::None,      -1,            defaultFieldColor(16));
    f << makeField("GYRO_RATE_Z", QStringLiteral("IMU5136速度Z"),   FieldGroup::Gyro,   FieldKind::Int16,  QStringLiteral("°/s"), 36, CorrectSource::None,      -1,            defaultFieldColor(17));
    // ── LINS358 惯导（可作为修正来源；旧帧不存在，legacyOffset=-1） ──
    f << makeField("LINS_ANG_X",  QStringLiteral("LINS358角度X"),   FieldGroup::Gyro,   FieldKind::Int16,  QStringLiteral("°"),   38, CorrectSource::None,      -1,            defaultFieldColor(18), AngleWrap::None, -1);
    f << makeField("LINS_ANG_Y",  QStringLiteral("LINS358角度Y"),   FieldGroup::Gyro,   FieldKind::Int16,  QStringLiteral("°"),   40, CorrectSource::None,      -1,            defaultFieldColor(19), AngleWrap::None, -1);
    f << makeField("LINS_ANG_Z",  QStringLiteral("LINS358角度Z"),   FieldGroup::Gyro,   FieldKind::Int16,  QStringLiteral("°"),   42, CorrectSource::None,      -1,            defaultFieldColor(20), AngleWrap::None, -1);
    f << makeField("LINS_RATE_X", QStringLiteral("LINS358速度X"),   FieldGroup::Gyro,   FieldKind::Int16,  QStringLiteral("°/s"), 44, CorrectSource::None,      -1,            defaultFieldColor(21), AngleWrap::None, -1);
    f << makeField("LINS_RATE_Y", QStringLiteral("LINS358速度Y"),   FieldGroup::Gyro,   FieldKind::Int16,  QStringLiteral("°/s"), 46, CorrectSource::None,      -1,            defaultFieldColor(22), AngleWrap::None, -1);
    f << makeField("LINS_RATE_Z", QStringLiteral("LINS358速度Z"),   FieldGroup::Gyro,   FieldKind::Int16,  QStringLiteral("°/s"), 48, CorrectSource::None,      -1,            defaultFieldColor(23), AngleWrap::None, -1);
    // 信标：新帧偏移 50，旧帧偏移 38（最后一个参数显式指定旧帧偏移）
    f << makeField("BEACON_PWR",  QStringLiteral("信标功率值"),     FieldGroup::Beacon, FieldKind::Int16,  QStringLiteral("dBm"), 50,  CorrectSource::None, -1, defaultFieldColor(24), AngleWrap::None, 38);

    cfg.header0 = 0xAA;
    cfg.header1 = 0x55;
    cfg.tail0 = 0x66;
    cfg.tail1 = 0xAA;
    cfg.frameLength = 54;
    cfg.legacyFrameLength = 42;
    cfg.insCorrectEnabled = true;
    cfg.correctionMode = CorrectionMode::ImuOnly; // 默认与旧版一致，仅使用 IMU5136
    return cfg;
}

int FrameConfig::indexOf(const QString &key) const
{
    for (int i = 0; i < fields.size(); ++i) {
        if (fields.at(i).key == key)
            return i;
    }
    return -1;
}

int FrameConfig::gyroAngleIndex(int gyroAxis) const
{
    static const char *keys[3] = {"GYRO_ANG_X", "GYRO_ANG_Y", "GYRO_ANG_Z"};
    if (gyroAxis < 0 || gyroAxis > 2)
        return -1;
    return indexOf(QLatin1String(keys[gyroAxis]));
}

int FrameConfig::gyroRateIndex(int gyroAxis) const
{
    static const char *keys[3] = {"GYRO_RATE_X", "GYRO_RATE_Y", "GYRO_RATE_Z"};
    if (gyroAxis < 0 || gyroAxis > 2)
        return -1;
    return indexOf(QLatin1String(keys[gyroAxis]));
}

int FrameConfig::linsAngleIndex(int gyroAxis) const
{
    static const char *keys[3] = {"LINS_ANG_X", "LINS_ANG_Y", "LINS_ANG_Z"};
    if (gyroAxis < 0 || gyroAxis > 2)
        return -1;
    return indexOf(QLatin1String(keys[gyroAxis]));
}

int FrameConfig::linsRateIndex(int gyroAxis) const
{
    static const char *keys[3] = {"LINS_RATE_X", "LINS_RATE_Y", "LINS_RATE_Z"};
    if (gyroAxis < 0 || gyroAxis > 2)
        return -1;
    return indexOf(QLatin1String(keys[gyroAxis]));
}

bool FrameConfig::isValid(QString *why) const
{
    const auto fail = [why](const QString &msg) {
        if (why)
            *why = msg;
        return false;
    };

    if (frameLength < 4)
        return fail(QStringLiteral("新帧长必须大于等于 4 字节"));
    if (legacyFrameLength < 4)
        return fail(QStringLiteral("旧帧长必须大于等于 4 字节"));
    if (correctionMode != CorrectionMode::ImuOnly && correctionMode != CorrectionMode::LinsOnly
        && correctionMode != CorrectionMode::ImuAndLins) {
        return fail(QStringLiteral("惯导修正来源非法"));
    }
    if (fields.isEmpty())
        return fail(QStringLiteral("字段映射表为空"));
    // 每个字段占 2 字节，新帧除 4 字节帧头帧尾外最多容纳这么多字段。
    if (fields.size() > (frameLength - 4) / 2)
        return fail(QStringLiteral("字段数量超出新帧长允许范围"));

    QSet<QString> keys;
    for (int i = 0; i < fields.size(); ++i) {
        const FieldDef &f = fields.at(i);
        const QString label = f.name.isEmpty() ? f.key : f.name;
        if (f.key.isEmpty())
            return fail(QStringLiteral("第 %1 个字段缺少变量名").arg(i + 1));
        if (keys.contains(f.key))
            return fail(QStringLiteral("字段变量名重复：%1").arg(f.key));
        keys.insert(f.key);
        if (!(f.scale > 0.0) || !std::isfinite(f.scale))
            return fail(QStringLiteral("字段“%1”换算系数必须为正的有限数（当前 %2）")
                            .arg(label)
                            .arg(f.scale));
        if (f.offset < 2 || f.offset + 2 > frameLength - 2)
            return fail(QStringLiteral("新帧中字段“%1”偏移 %2 越界（合法范围 2~%3）")
                            .arg(label)
                            .arg(f.offset)
                            .arg(frameLength - 4));
        if (f.legacyOffset < -1)
            return fail(QStringLiteral("字段“%1”旧帧偏移非法（%2）；无此字段时应为 -1")
                            .arg(label)
                            .arg(f.legacyOffset));
        if (f.legacyOffset >= 0
            && (f.legacyOffset < 2 || f.legacyOffset + 2 > legacyFrameLength - 2)) {
            return fail(QStringLiteral("旧帧中字段“%1”偏移 %2 越界（合法范围 2~%3，-1 表示无此字段）")
                            .arg(label)
                            .arg(f.legacyOffset)
                            .arg(legacyFrameLength - 4));
        }
        if (f.correct != CorrectSource::None && (f.mechAxis < 0 || f.mechAxis > 2))
            return fail(QStringLiteral("字段“%1”配置了惯导修正，但机械轴非法（%2）")
                            .arg(label)
                            .arg(f.mechAxis));
    }

    // 校验某一种帧布局：len=帧长；useLegacy=true 用旧帧偏移，false 用新帧偏移。
    // 偏移为 -1 的字段在该布局中不存在（跳过）。
    const auto validateLayout = [&](int len, bool useLegacy, const QString &tag) -> bool {
        QSet<int> usedBytes;
        for (int i = 0; i < fields.size(); ++i) {
            const FieldDef &fld = fields.at(i);
            const int off = useLegacy ? fld.legacyOffset : fld.offset;
            if (off < 0)
                continue;
            const QString label = fld.name.isEmpty() ? fld.key : fld.name;
            if (off < 2 || off + 2 > len - 2)
                return fail(QStringLiteral("%1中字段“%2”偏移 %3 越界（合法范围 2~%4）")
                                .arg(tag, label)
                                .arg(off)
                                .arg(len - 4));
            for (int b = off; b < off + 2; ++b) {
                if (usedBytes.contains(b))
                    return fail(QStringLiteral("%1中字段“%2”的字节偏移 %3 与其它字段重叠")
                                    .arg(tag, label)
                                    .arg(b));
                usedBytes.insert(b);
            }
        }
        return true;
    };

    if (!validateLayout(frameLength, false, QStringLiteral("新帧")))
        return false;
    if (!validateLayout(legacyFrameLength, true, QStringLiteral("旧帧")))
        return false;

    for (int a = 0; a < 3; ++a) {
        if (gyroAxisForMech[a] < 0 || gyroAxisForMech[a] > 2)
            return fail(QStringLiteral("%1轴对应的惯导轴配置非法").arg(mechAxisName(a)));
    }
    return true;
}

void FrameConfig::setScaleAll(double scale)
{
    for (FieldDef &f : fields)
        f.scale = scale;
}

FieldDef FrameConfig::field(const QString &key) const
{
    const int idx = indexOf(key);
    if (idx >= 0)
        return fields.at(idx);
    return FieldDef();
}

QString fieldGroupName(FieldGroup group)
{
    switch (group) {
    case FieldGroup::Angle:
        return QStringLiteral("角度类");
    case FieldGroup::Rate:
        return QStringLiteral("速度类");
    case FieldGroup::Gyro:
        return QStringLiteral("惯导类");
    case FieldGroup::Beacon:
        return QStringLiteral("信标");
    }
    return QString();
}

QString mechAxisName(int mechAxis)
{
    switch (mechAxis) {
    case MechAzimuth:
        return QStringLiteral("方位");
    case MechElevation:
        return QStringLiteral("俯仰");
    case MechRoll:
        return QStringLiteral("横滚");
    default:
        return QStringLiteral("未知");
    }
}

QString gyroAxisName(int gyroAxis)
{
    switch (gyroAxis) {
    case GyroX:
        return QStringLiteral("X");
    case GyroY:
        return QStringLiteral("Y");
    case GyroZ:
        return QStringLiteral("Z");
    default:
        return QStringLiteral("?");
    }
}

QString correctionModeName(CorrectionMode mode)
{
    switch (mode) {
    case CorrectionMode::ImuOnly:
        return QStringLiteral("IMU5136");
    case CorrectionMode::LinsOnly:
        return QStringLiteral("LINS358");
    case CorrectionMode::ImuAndLins:
        return QStringLiteral("IMU5136+LINS358");
    }
    return QStringLiteral("未知");
}

QColor defaultFieldColor(int fieldIndex)
{
    if (fieldIndex >= 0 && fieldIndex < 25)
        return kPalette[fieldIndex];
    return QColor(0x33, 0x33, 0x33);
}

} // namespace xn
