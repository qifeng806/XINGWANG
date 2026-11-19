#include "appsettings.h"

#include <QSettings>

#include <cmath>

namespace xn {

namespace {

QSettings *settings()
{
    // 显式使用 defaultFormat()：生产环境默认是注册表（保持原行为），
    // 测试可通过 QSettings::setDefaultFormat(IniFormat) + setPath 隔离到临时目录。
    static QSettings instance{QSettings::defaultFormat(), QSettings::UserScope,
                              QStringLiteral("XingWang"), QStringLiteral("TurntableAnalyzer")};
    return &instance;
}

//! 配置结构版本：
//!   0/1 = 旧版（无版本号；42 字节旧帧配置只有 19 个字段记录）
//!   2   = 当前版本（54/42 双帧长、25 字段、完整字段元数据、修正来源）
constexpr int kConfigVersion = 2;

/*! \brief 读取枚举值；越界时返回 fallback */
int readEnum(QSettings *s, const QString &key, int fallback, int minValue, int maxValue,
             bool *invalid)
{
    const int value = s->value(key, fallback).toInt();
    if (value < minValue || value > maxValue) {
        if (invalid)
            *invalid = true;
        return fallback;
    }
    return value;
}

} // namespace

FrameConfig AppSettings::loadFrameConfig()
{
    FrameConfig cfg = FrameConfig::defaultConfig();
    QSettings *s = settings();
    bool dirty = false;

    const int version = s->value(QStringLiteral("protocol/configVersion"), 0).toInt();
    const int savedFrameLength =
        s->value(QStringLiteral("protocol/frameLength"), cfg.frameLength).toInt();
    const int savedLegacyFrameLength =
        s->value(QStringLiteral("protocol/legacyFrameLength"), cfg.legacyFrameLength).toInt();

    cfg.header0 = quint8(s->value(QStringLiteral("protocol/header0"), cfg.header0).toUInt());
    cfg.header1 = quint8(s->value(QStringLiteral("protocol/header1"), cfg.header1).toUInt());
    cfg.tail0 = quint8(s->value(QStringLiteral("protocol/tail0"), cfg.tail0).toUInt());
    cfg.tail1 = quint8(s->value(QStringLiteral("protocol/tail1"), cfg.tail1).toUInt());
    cfg.frameLength = savedFrameLength;
    cfg.legacyFrameLength = savedLegacyFrameLength;
    cfg.insCorrectEnabled = s->value(QStringLiteral("correct/enabled"), cfg.insCorrectEnabled).toBool();
    bool modeInvalid = false;
    cfg.correctionMode = CorrectionMode(
        readEnum(s, QStringLiteral("correct/source"), int(cfg.correctionMode),
                 int(CorrectionMode::ImuOnly), int(CorrectionMode::ImuAndLins), &modeInvalid));
    if (modeInvalid) {
        qWarning("已保存的惯导修正来源非法，已恢复默认值（仅 IMU5136）");
        dirty = true;
    }
    for (int a = 0; a < 3; ++a) {
        cfg.axisCorrectEnabled[a] =
            s->value(QStringLiteral("correct/axis%1").arg(a), cfg.axisCorrectEnabled[a]).toBool();
        cfg.gyroAxisForMech[a] =
            s->value(QStringLiteral("correct/gyroForMech%1").arg(a), cfg.gyroAxisForMech[a]).toInt();
    }

    // ─────────────────────────────────────────────────────────────
    // 版本迁移（一次性）
    //
    // 旧版（无 configVersion）在 42 字节时代保存 19 个字段：前 18 个机械/IMU
    // 字段偏移与新帧相同，信标功率值在旧帧偏移 38。升级到 54 字节/25 字段后，
    // 若直接沿用 offset=38 的信标，会与新增的 LINS358 角度 X（38~39）重叠，
    // 从而触发整表回退，用户配置全部丢失。
    // 这里只迁移“19 个字段 + 旧帧长 42”的旧配置：新帧长改为 54，信标偏移
    // 38→50，LINS358 六字段保留默认值，其余字段原样保留。
    // ─────────────────────────────────────────────────────────────
    const int savedFieldCount = s->beginReadArray(QStringLiteral("protocol/fields"));
    const bool oldSchema = version < kConfigVersion && savedFieldCount > 0 && savedFieldCount <= 19
        && savedFrameLength == 42;
    if (oldSchema) {
        cfg.frameLength = 54;
        cfg.legacyFrameLength = 42;
        dirty = true;
        qWarning("检测到旧版 42 字节/19 字段配置，正在迁移为 54/42 双帧长协议");
    }

    // 字段映射：按变量名匹配；逐字段校验，单个字段非法时只丢弃该字段的覆盖值。
    // 名称、单位、新旧偏移、类型、系数、修正来源、机械轴、分组、颜色均持久化。
    for (int i = 0; i < savedFieldCount; ++i) {
        s->setArrayIndex(i);
        const QString key = s->value(QStringLiteral("key")).toString();
        const int index = cfg.indexOf(key);
        if (index < 0)
            continue;
        FieldDef &f = cfg.fields[index];

        int offset = s->value(QStringLiteral("offset"), f.offset).toInt();
        int legacyOffset = s->value(QStringLiteral("legacyOffset"), f.legacyOffset).toInt();
        const double scale = s->value(QStringLiteral("scale"), f.scale).toDouble();

        // 旧配置迁移：信标从旧帧偏移 38 移到新帧偏移 50
        if (oldSchema && key == QStringLiteral("BEACON_PWR") && offset == 38) {
            offset = f.offset;
            dirty = true;
        }

        if (offset < 2 || offset + 2 > cfg.frameLength - 2 || !(scale > 0.0)
            || !std::isfinite(scale)) {
            qWarning("字段 %s 的已保存配置非法（新帧偏移 %d，系数 %g），已忽略该项",
                     qPrintable(key), offset, scale);
            dirty = true;
            continue;
        }
        const bool legacyValid = legacyOffset == -1
            || (legacyOffset >= 2 && legacyOffset + 2 <= cfg.legacyFrameLength - 2);
        if (!legacyValid) {
            qWarning("字段 %s 的旧帧偏移 %d 非法，已恢复默认值 %d", qPrintable(key), legacyOffset,
                     f.legacyOffset);
            legacyOffset = f.legacyOffset;
            dirty = true;
        }

        const QString name = s->value(QStringLiteral("name"), f.name).toString();
        const QString unit = s->value(QStringLiteral("unit"), f.unit).toString();
        const QString colorName = s->value(QStringLiteral("color"), f.color.name()).toString();
        const QColor color = QColor::isValidColorName(colorName) ? QColor(colorName) : f.color;
        bool enumInvalid = false;
        const int wrap = readEnum(s, QStringLiteral("wrap"), int(f.wrap),
                                  int(AngleWrap::None), int(AngleWrap::Signed180), &enumInvalid);
        const int correct = readEnum(s, QStringLiteral("correct"), int(f.correct),
                                     int(CorrectSource::None), int(CorrectSource::GyroRate),
                                     &enumInvalid);
        const int mechAxis = readEnum(s, QStringLiteral("mechAxis"), f.mechAxis, -1, 2, &enumInvalid);
        const int group = readEnum(s, QStringLiteral("group"), int(f.group),
                                   int(FieldGroup::Angle), int(FieldGroup::Beacon), &enumInvalid);
        if (enumInvalid) {
            qWarning("字段 %s 的已保存枚举配置非法，已恢复默认值", qPrintable(key));
            dirty = true;
        }

        f.offset = offset;
        f.legacyOffset = legacyOffset;
        f.kind = s->value(QStringLiteral("signed"), f.kind == FieldKind::Int16).toBool()
            ? FieldKind::Int16
            : FieldKind::UInt16;
        f.scale = scale;
        if (!name.isEmpty())
            f.name = name;
        f.unit = unit;
        if (color.isValid())
            f.color = color;
        f.wrap = AngleWrap(wrap);
        f.correct = CorrectSource(correct);
        f.mechAxis = mechAxis;
        f.group = FieldGroup(group);
    }
    s->endArray();

    if (version != kConfigVersion && !oldSchema)
        dirty = true; // 仅补写版本号，不改变已有配置

    // ─────────────────────────────────────────────────────────────
    // 历史协议笔误修复（一次性迁移）
    //
    // v1.0.0 之前，信标功率值被误定义为 UInt16（源自计划文档笔误，
    // 而串口发送源码实际使用 (int16_t) 强制转换）。旧配置里保存的
    // "signed=false" 会在上面的循环里覆盖新默认值，导致信标值恒为正、
    // 量程完全错误（实测应为 -66.38 dBm，旧代码显示 +588.98）。
    // 此处强制纠正，避免老用户升级后继续解析错误。
    // ─────────────────────────────────────────────────────────────
    const int beaconIndex = cfg.indexOf(QStringLiteral("BEACON_PWR"));
    if (beaconIndex >= 0 && cfg.fields.at(beaconIndex).kind != FieldKind::Int16) {
        qWarning("检测到旧版本的信标功率值类型配置（UInt16），已自动修正为 Int16");
        cfg.fields[beaconIndex].kind = FieldKind::Int16;
        dirty = true;
    }

    QString why;
    if (!cfg.isValid(&why)) {
        // 依然不合法（例如旧版本保存的字段偏移互相重叠）→ 整体回退并覆盖写回，
        // 避免每次启动都重复丢弃配置。
        qWarning("已保存的帧协议配置非法（%s），已恢复默认协议", qPrintable(why));
        cfg = FrameConfig::defaultConfig();
        dirty = true;
    }
    if (dirty)
        saveFrameConfig(cfg);
    return cfg;
}

void AppSettings::saveFrameConfig(const FrameConfig &cfg)
{
    QSettings *s = settings();
    s->setValue(QStringLiteral("protocol/configVersion"), kConfigVersion);
    s->setValue(QStringLiteral("protocol/header0"), cfg.header0);
    s->setValue(QStringLiteral("protocol/header1"), cfg.header1);
    s->setValue(QStringLiteral("protocol/tail0"), cfg.tail0);
    s->setValue(QStringLiteral("protocol/tail1"), cfg.tail1);
    s->setValue(QStringLiteral("protocol/frameLength"), cfg.frameLength);
    s->setValue(QStringLiteral("protocol/legacyFrameLength"), cfg.legacyFrameLength);
    s->setValue(QStringLiteral("correct/enabled"), cfg.insCorrectEnabled);
    s->setValue(QStringLiteral("correct/source"), int(cfg.correctionMode));
    for (int a = 0; a < 3; ++a) {
        s->setValue(QStringLiteral("correct/axis%1").arg(a), cfg.axisCorrectEnabled[a]);
        s->setValue(QStringLiteral("correct/gyroForMech%1").arg(a), cfg.gyroAxisForMech[a]);
    }

    s->beginWriteArray(QStringLiteral("protocol/fields"), cfg.fields.size());
    for (int i = 0; i < cfg.fields.size(); ++i) {
        const FieldDef &f = cfg.fields.at(i);
        s->setArrayIndex(i);
        s->setValue(QStringLiteral("key"), f.key);
        s->setValue(QStringLiteral("name"), f.name);
        s->setValue(QStringLiteral("unit"), f.unit);
        s->setValue(QStringLiteral("offset"), f.offset);
        s->setValue(QStringLiteral("legacyOffset"), f.legacyOffset);
        s->setValue(QStringLiteral("signed"), f.kind == FieldKind::Int16);
        s->setValue(QStringLiteral("scale"), f.scale);
        s->setValue(QStringLiteral("wrap"), int(f.wrap));
        s->setValue(QStringLiteral("correct"), int(f.correct));
        s->setValue(QStringLiteral("mechAxis"), f.mechAxis);
        s->setValue(QStringLiteral("group"), int(f.group));
        s->setValue(QStringLiteral("color"), f.color.name());
    }
    s->endArray();
    s->sync();
}

DisplayConfig AppSettings::loadDisplayConfig()
{
    DisplayConfig d;
    QSettings *s = settings();
    d.lineWidth = s->value(QStringLiteral("display/lineWidth"), d.lineWidth).toDouble();
    d.gridVisible = s->value(QStringLiteral("display/grid"), d.gridVisible).toBool();
    d.subGridVisible = s->value(QStringLiteral("display/subGrid"), d.subGridVisible).toBool();
    d.antialiasing = s->value(QStringLiteral("display/antialiasing"), d.antialiasing).toBool();
    d.openGl = s->value(QStringLiteral("display/openGl"), d.openGl).toBool();
    d.showRawValues = s->value(QStringLiteral("display/showRawValues"), d.showRawValues).toBool();
    d.showBias = s->value(QStringLiteral("display/showBias"), d.showBias).toBool();
    d.pngWidth = s->value(QStringLiteral("display/pngWidth"), d.pngWidth).toInt();
    d.pngHeight = s->value(QStringLiteral("display/pngHeight"), d.pngHeight).toInt();
    d.decimationThreshold =
        s->value(QStringLiteral("display/decimationThreshold"), d.decimationThreshold).toInt();
    d.maxPointsPerCurve =
        s->value(QStringLiteral("display/maxPointsPerCurve"), d.maxPointsPerCurve).toInt();
    if (d.lineWidth <= 0.0)
        d.lineWidth = 1.0;
    if (d.maxPointsPerCurve < 1000)
        d.maxPointsPerCurve = 1000;
    return d;
}

void AppSettings::saveDisplayConfig(const DisplayConfig &cfg)
{
    QSettings *s = settings();
    s->setValue(QStringLiteral("display/lineWidth"), cfg.lineWidth);
    s->setValue(QStringLiteral("display/grid"), cfg.gridVisible);
    s->setValue(QStringLiteral("display/subGrid"), cfg.subGridVisible);
    s->setValue(QStringLiteral("display/antialiasing"), cfg.antialiasing);
    s->setValue(QStringLiteral("display/openGl"), cfg.openGl);
    s->setValue(QStringLiteral("display/showRawValues"), cfg.showRawValues);
    s->setValue(QStringLiteral("display/showBias"), cfg.showBias);
    s->setValue(QStringLiteral("display/pngWidth"), cfg.pngWidth);
    s->setValue(QStringLiteral("display/pngHeight"), cfg.pngHeight);
    s->setValue(QStringLiteral("display/decimationThreshold"), cfg.decimationThreshold);
    s->setValue(QStringLiteral("display/maxPointsPerCurve"), cfg.maxPointsPerCurve);
    s->sync();
}

QVector<double> AppSettings::loadFieldBias(int fieldCount)
{
    QVector<double> bias(qMax(0, fieldCount), 0.0);
    if (fieldCount <= 0)
        return bias;
    QSettings *s = settings();
    const int size = s->beginReadArray(QStringLiteral("bias/fields"));
    for (int i = 0; i < size && i < bias.size(); ++i) {
        s->setArrayIndex(i);
        bool ok = false;
        const double v = s->value(QStringLiteral("value")).toDouble(&ok);
        if (ok && std::isfinite(v))
            bias[i] = v;
    }
    s->endArray();
    return bias;
}

void AppSettings::saveFieldBias(const QVector<double> &bias)
{
    QSettings *s = settings();
    s->beginWriteArray(QStringLiteral("bias/fields"), bias.size());
    for (int i = 0; i < bias.size(); ++i) {
        s->setArrayIndex(i);
        s->setValue(QStringLiteral("value"), bias.at(i));
    }
    s->endArray();
    s->sync();
}

QString AppSettings::lastDirectory()
{
    return settings()->value(QStringLiteral("path/lastDir")).toString();
}

void AppSettings::setLastDirectory(const QString &dir)
{
    settings()->setValue(QStringLiteral("path/lastDir"), dir);
}

void AppSettings::resetAll()
{
    settings()->clear();
    settings()->sync();
}

} // namespace xn
