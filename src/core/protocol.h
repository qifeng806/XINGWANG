#ifndef XN_PROTOCOL_H
#define XN_PROTOCOL_H

/*!
 * \file protocol.h
 * \brief 星网转台串口帧协议定义：字段映射表、帧配置、配色方案。
 *
 * 本协议支持两种帧长（均为大端序，高位在前），解析器会自动识别：
 *
 * 【新帧】54 字节，同时包含 IMU5136 与 LINS358 两套惯导：
 *   [0]     0xAA  帧头 1
 *   [1]     0x55  帧头 2
 *   [2..3]  方位目标角度      uint16 /100  °      （惯导角度修正）
 *   [4..5]  方位编码器角度    uint16 /100  °
 *   [6..7]  方位运行速度      int16  /100  °/s
 *   [8..9]  方位前馈速度      int16  /100  °/s    （惯导速度修正）
 *   [10..11]俯仰目标角度      uint16 /100  °      （惯导角度修正）
 *   [12..13]俯仰编码器角度    uint16 /100  °
 *   [14..15]俯仰运行速度      int16  /100  °/s
 *   [16..17]俯仰前馈速度      int16  /100  °/s    （惯导速度修正）
 *   [18..19]横滚目标角度      uint16 /100  °      （惯导角度修正；解析后环绕到 ±180）
 *   [20..21]横滚编码器角度    uint16 /100  °      （解析后环绕到 ±180）
 *   [22..23]横滚运行速度      int16  /100  °/s
 *   [24..25]横滚前馈速度      int16  /100  °/s    （惯导速度修正）
 *   ── IMU5136 惯导 ──
 *   [26..27] IMU5136 角度 X   int16  /100  °
 *   [28..29] IMU5136 角度 Y   int16  /100  °
 *   [30..31] IMU5136 角度 Z   int16  /100  °
 *   [32..33] IMU5136 速度 X   int16  /100  °/s
 *   [34..35] IMU5136 速度 Y   int16  /100  °/s
 *   [36..37] IMU5136 速度 Z   int16  /100  °/s
 *   ── LINS358 惯导（字段排布与 IMU5136 完全相同） ──
 *   [38..39] LINS358 角度 X   int16  /100  °
 *   [40..41] LINS358 角度 Y   int16  /100  °
 *   [42..43] LINS358 角度 Z   int16  /100  °
 *   [44..45] LINS358 速度 X   int16  /100  °/s
 *   [46..47] LINS358 速度 Y   int16  /100  °/s
 *   [48..49] LINS358 速度 Z   int16  /100  °/s
 *   [50..51]信标功率值        int16  /100  dBm
 *   [52]    0x66  帧尾 1
 *   [53]    0xAA  帧尾 2
 *
 * 【旧帧】42 字节，仅含 IMU5136（无 LINS358）：
 *   [0..37] 与新帧完全相同（到 IMU5136 速度 Z 为止）
 *   [38..39]信标功率值        int16  /100  dBm
 *   [40]    0x66  帧尾 1
 *   [41]    0xAA  帧尾 2
 *
 * 字段同时给出“新帧偏移 offset”与“旧帧偏移 legacyOffset”；
 * LINS358 各字段在旧帧中不存在（legacyOffset = -1），解析旧帧时取值为 0。
 *
 * 两套惯导字段始终保留在字段表中，修正来源由 CorrectionMode 选择：
 *   - ImuOnly（默认）：修正值 = 机械量 + IMU5136，与加入 LINS358 之前的旧版行为一致；
 *   - LinsOnly：修正值 = 机械量 + LINS358；旧帧无 LINS358 时自动回退为 IMU5136；
 *   - ImuAndLins：修正值 = 机械量 + IMU5136 + LINS358。
 */

#include <QColor>
#include <QString>
#include <QVector>

#include <cmath>

namespace xn {

/*! \brief 字段数值类型（帧内均为大端 2 字节） */
enum class FieldKind {
    UInt16, //!< 无符号整数
    Int16   //!< 有符号整数（二进制补码）
};

/*! \brief 角度环绕方式（决定解析后如何归一化角度显示范围） */
enum class AngleWrap {
    None,      //!< 不环绕（保持原始表示，如方位角的 0~360）
    Signed180  //!< 环绕到 ±180：超过 180 的部分减去 360，显示为负值（如横滚角）
};

/*! \brief 惯导修正来源（单个字段是否参与修正） */
enum class CorrectSource {
    None,      //!< 不修正
    GyroAngle, //!< 叠加对应轴惯导角度（目标角度类字段）
    GyroRate   //!< 叠加对应轴惯导速度（前馈速度类字段）
};

/*! \brief 惯导数据源选择（全局） */
enum class CorrectionMode {
    ImuOnly = 0,   //!< 仅 IMU5136（默认，保持旧版行为）
    LinsOnly = 1,  //!< 仅 LINS358；旧帧无 LINS358 时自动回退为 IMU5136
    ImuAndLins = 2 //!< IMU5136 + LINS358 叠加
};

/*! \brief 字段分组：决定配色（暖色/冷色/中性色/高亮）与复选框分组 */
enum class FieldGroup {
    Angle, //!< 角度类
    Rate,  //!< 速度类
    Gyro,  //!< 惯导类
    Beacon //!< 信标功率
};

/*! \brief 单个物理量的定义（配置化字段映射表的行） */
struct FieldDef {
    QString key;                                    //!< 英文变量名（与源码变量对应）
    QString name;                                   //!< 中文名称
    QString unit;                                   //!< 工程单位
    FieldKind kind = FieldKind::UInt16;             //!< 数值类型
    double scale = 100.0;                           //!< 换算系数（原始值 / scale）
    AngleWrap wrap = AngleWrap::None;               //!< 角度环绕方式（角度类字段用）
    int offset = 0;                                 //!< 新帧（54 字节）内字节偏移（从帧头算起）
    int legacyOffset = -1;                          //!< 旧帧（42 字节）内字节偏移；-1 表示该字段在旧帧中不存在
    CorrectSource correct = CorrectSource::None;    //!< 惯导修正来源
    int mechAxis = -1;                              //!< 机械轴：0=方位 1=俯仰 2=横滚，-1=非机械轴
    FieldGroup group = FieldGroup::Angle;           //!< 分组
    QColor color = QColor(0, 0, 0);                 //!< 曲线颜色
};

/*! \brief 机械轴 / 惯导轴 索引约定 */
enum MechAxis { MechAzimuth = 0, MechElevation = 1, MechRoll = 2 };
enum GyroAxis { GyroX = 0, GyroY = 1, GyroZ = 2 };

/*! \brief 字段定义完整比较（用于配置变更检测，避免逐项比较时漏掉成员） */
inline bool operator==(const FieldDef &a, const FieldDef &b)
{
    return a.key == b.key && a.name == b.name && a.unit == b.unit && a.kind == b.kind
        && qFuzzyCompare(a.scale, b.scale) && a.wrap == b.wrap && a.offset == b.offset
        && a.legacyOffset == b.legacyOffset
        && a.correct == b.correct && a.mechAxis == b.mechAxis && a.group == b.group
        && a.color == b.color;
}

inline bool operator!=(const FieldDef &a, const FieldDef &b)
{
    return !(a == b);
}

/*! \brief 整帧协议配置（帧头帧尾、帧长、字段映射表、惯导修正规则） */
struct FrameConfig {
    quint8 header0 = 0xAA;                      //!< 帧头字节 1
    quint8 header1 = 0x55;                      //!< 帧头字节 2
    quint8 tail0 = 0x66;                        //!< 帧尾字节 1
    quint8 tail1 = 0xAA;                        //!< 帧尾字节 2
    int frameLength = 54;                       //!< 新帧长（字节，含 IMU5136 + LINS358）
    int legacyFrameLength = 42;                 //!< 旧帧长（字节，仅含 IMU5136）
    QVector<FieldDef> fields;                   //!< 字段映射表（按帧内顺序）

    bool insCorrectEnabled = true;              //!< 惯导修正总开关
    CorrectionMode correctionMode = CorrectionMode::ImuOnly; //!< 惯导数据源（默认仅 IMU5136）
    bool axisCorrectEnabled[3] = {true, true, true}; //!< 三轴独立修正开关（方位/俯仰/横滚）
    int gyroAxisForMech[3] = {GyroX, GyroY, GyroZ};  //!< 机械轴 -> 惯导轴 对应关系

    /*! \brief 生成与当前实测帧协议一致的默认配置（25 个物理量，含两套惯导） */
    static FrameConfig defaultConfig();

    /*! \brief 字段数量 */
    int fieldCount() const { return fields.size(); }

    /*! \brief 按变量名查找字段索引，未找到返回 -1 */
    int indexOf(const QString &key) const;

    /*! \brief 惯导角度字段索引（gyroAxis: 0=X 1=Y 2=Z），未找到返回 -1 */
    int gyroAngleIndex(int gyroAxis) const;

    /*! \brief 惯导速度字段索引（gyroAxis: 0=X 1=Y 2=Z），未找到返回 -1 */
    int gyroRateIndex(int gyroAxis) const;

    /*! \brief LINS358 惯导角度字段索引（gyroAxis: 0=X 1=Y 2=Z），未找到返回 -1 */
    int linsAngleIndex(int gyroAxis) const;

    /*! \brief LINS358 惯导速度字段索引（gyroAxis: 0=X 1=Y 2=Z），未找到返回 -1 */
    int linsRateIndex(int gyroAxis) const;

    /*! \brief 校验配置是否自洽；不自洽时通过 why 返回原因 */
    bool isValid(QString *why = nullptr) const;

    /*! \brief 批量设置所有字段换算系数 */
    void setScaleAll(double scale);

    /*! \brief 依据变量名取字段（不存在时返回默认 FieldDef） */
    FieldDef field(const QString &key) const;
};

/*!
 * \brief 将角度归一化到 ±180（结果区间为 (-180, 180]）
 *
 * 用于把 0~360 表示的姿态角（如横滚）换算成 ±180：
 * 超过 180 的部分减去 360、显示为负值（例如 350° → -10°，180° 保持 +180°）。
 *
 * \param angle 输入角度（度，可超出 ±180 任意范围）
 * \return 归一化后的角度，范围 (-180, 180]
 */
inline double wrapAngle180(double angle)
{
    angle = std::fmod(angle, 360.0); // 先折叠到 (-360, 360)
    if (angle > 180.0)
        angle -= 360.0;              // (180, 360) → (-180, 0)
    else if (angle <= -180.0)
        angle += 360.0;              // (-360, -180] → (0, 180]
    return angle;
}

/*! \brief 分组显示名 */
QString fieldGroupName(FieldGroup group);

/*! \brief 机械轴显示名 */
QString mechAxisName(int mechAxis);

/*! \brief 惯导轴显示名 */
QString gyroAxisName(int gyroAxis);

/*! \brief 修正来源显示名 */
QString correctionModeName(CorrectionMode mode);

/*! \brief 默认配色（暖色角度 / 冷色速度 / 中性惯导 / 高亮信标） */
QColor defaultFieldColor(int fieldIndex);

} // namespace xn

#endif // XN_PROTOCOL_H
