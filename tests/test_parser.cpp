/*!
 * \file test_parser.cpp
 * \brief 解析核心单元测试：协议默认值、合成帧解析、异常帧/残帧、真实数据交叉验证、CSV 导出、配置持久化。
 *
 * 真实数据来自随工程提供的 sample 目录：
 *   - 20261010_54byte_3841frames.txt：当前 54 字节新帧（IMU5136 + LINS358），3841 帧；
 *   - 20260923_42byte_1304frames.txt：42 字节旧帧（仅 IMU5136），1304 帧。
 * 期望值由独立实现的参考脚本 tools/reference_parse.py 生成，二者互为验证。
 */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>

#include "core/appsettings.h"
#include "core/datamanager.h"
#include "core/framedata.h"
#include "core/frameparser.h"
#include "core/hexfilereader.h"
#include "core/protocol.h"

#include "samplelocator.h"

#include <memory>

using namespace xn;

namespace {

/*! \brief 定位 54 字节双惯导样例（见 samplelocator.h；可用环境变量 XW_SAMPLE 指定） */
QString locateSampleFile()
{
    return xwtest::locateDualSampleFile();
}

/*! \brief 定位 42 字节旧帧样例（可用环境变量 XW_SAMPLE_LEGACY 指定） */
QString locateLegacySampleFile()
{
    return xwtest::locateLegacySampleFile();
}

/*! \brief 按默认协议构造一帧数据（values 为未经换算的原始整数值） */
QByteArray makeFrame(const FrameConfig &cfg, const QVector<int> &values)
{
    QByteArray frame(cfg.frameLength, '\0');
    frame[0] = char(cfg.header0);
    frame[1] = char(cfg.header1);
    for (int i = 0; i < values.size() && i < cfg.fields.size(); ++i) {
        const quint16 v = quint16(values.at(i));
        const int offset = cfg.fields.at(i).offset;
        frame[offset] = char((v >> 8) & 0xFF);
        frame[offset + 1] = char(v & 0xFF);
    }
    frame[cfg.frameLength - 2] = char(cfg.tail0);
    frame[cfg.frameLength - 1] = char(cfg.tail1);
    return frame;
}

/*! \brief 按旧协议（42 字节）构造一帧：LINS358 缺失，信标在偏移 38。
 *
 *  values 仍按 25 个字段索引给出；字段 18-23（LINS）因 legacyOffset<0 被跳过，
 *  字段 24（信标）写入 legacyOffset=38。
 */
QByteArray makeLegacyFrame(const FrameConfig &cfg, const QVector<int> &values)
{
    QByteArray frame(cfg.legacyFrameLength, '\0');
    frame[0] = char(cfg.header0);
    frame[1] = char(cfg.header1);
    for (int i = 0; i < values.size() && i < cfg.fields.size(); ++i) {
        const int offset = cfg.fields.at(i).legacyOffset;
        if (offset < 0)
            continue;
        const quint16 v = quint16(values.at(i));
        frame[offset] = char((v >> 8) & 0xFF);
        frame[offset + 1] = char(v & 0xFF);
    }
    frame[cfg.legacyFrameLength - 2] = char(cfg.tail0);
    frame[cfg.legacyFrameLength - 1] = char(cfg.tail1);
    return frame;
}

/*! \brief 参考实现给出的期望值（tools/reference_parse.py，54 字节样例，默认 imu 源） */
const double k54Frame0Raw[25] = {307.87, 307.86, 6.11, 4.13, 40.11, 40.16, -1.60, -0.89, 2.86, 2.80,
                                 4.78, 4.25, -0.69, 1.54, -1.15, -0.91, 3.09, -1.53, 2.97, 41.22,
                                 -113.23, 0.03, 0.88, 0.11, -62.80};
const double k54Frame0Imu[25] = {307.18, 307.86, 6.11, 3.22, 41.65, 40.16, -1.60, 2.20, 1.71, 2.80,
                                 4.78, 2.72, -0.69, 1.54, -1.15, -0.91, 3.09, -1.53, 2.97, 41.22,
                                 -113.23, 0.03, 0.88, 0.11, -62.80};
const double k54Frame0Both[25] = {310.15, 307.86, 6.11, 3.25, 82.87, 40.16, -1.60, 3.08, -111.52,
                                  2.80,   4.78,   2.83,  -0.69, 1.54, -1.15, -0.91, 3.09, -1.53,
                                  2.97,   41.22,  -113.23, 0.03, 0.88, 0.11, -62.80};
const double k54Frame1Raw[25] = {308.08, 308.02, 4.88, 4.05, 39.94, 40.15, -2.90, -0.85, 3.02, 3.08,
                                 4.87, 3.99, -0.74, 1.67, -1.21, -1.34, 3.10, -1.24, 3.03, 41.27,
                                 -113.14, 0.76, -0.92, 0.94, -63.00};
const double k54Frame1Imu[25] = {307.34, 308.02, 4.88, 2.71, 41.61, 40.15, -2.90, 2.25, 1.81, 3.08,
                                 4.87, 2.75, -0.74, 1.67, -1.21, -1.34, 3.10, -1.24, 3.03, 41.27,
                                 -113.14, 0.76, -0.92, 0.94, -63.00};
const double k54Frame100Raw[25] = {291.33, 291.23, -6.07, -7.43, 40.10, 40.13, -1.63, -0.84, -14.60,
                                   -14.89, -16.54, -15.01, 5.53, -10.67, 3.36, 4.55, -10.00, -2.40,
                                   3.10,   41.03,  -113.20, 0.65, -1.21, -0.80, -62.50};
const double k54Frame100Imu[25] = {296.86, 291.23, -6.07, -2.88, 29.43, 40.13, -1.63, -10.84, -11.24,
                                   -14.89, -16.54, -17.41, 5.53, -10.67, 3.36, 4.55, -10.00, -2.40,
                                   3.10,   41.03,  -113.20, 0.65, -1.21, -0.80, -62.50};
const double k54Frame3840Raw[25] = {304.99, 305.01, 0.22, 0.22, 40.52, 40.46, 1.44, 0.12, 0.43,
                                    0.42,   -0.80,  -0.26, -0.13, -0.41, 0.22, -0.27, 0.17, 0.02,
                                    4.03,   40.20,  -110.16, 0.24, 0.49, 0.32, -62.90};
const double k54Frame3840Imu[25] = {304.86, 305.01, 0.22, -0.05, 40.11, 40.46, 1.44, 0.29, 0.65,
                                    0.42,   -0.80,  -0.24, -0.13, -0.41, 0.22, -0.27, 0.17, 0.02,
                                    4.03,   40.20,  -110.16, 0.24, 0.49, 0.32, -62.90};

const double k54RawSums[25] = {1172411.74, 1172413.40, -137.78, -160.17, 150162.64,
                               150151.70,  206.02,      42.80,   1597.01, 1597.75,
                               -84.17,     -87.00,      1129.01, -356.66, -1108.99,
                               -2264.25,   -969.75,     293.71,  14243.28, 156752.62,
                               -429036.83, -171.91,     -118.50, 45.93,   -242239.29};
const double k54ImuSums[25] = {1173540.75, 1172413.40, -137.78, -2424.42, 149805.98,
                               150151.70,  206.02,      -926.95, 488.02,   1597.75,
                               -84.17,     206.71,      1129.01, -356.66,  -1108.99,
                               -2264.25,   -969.75,     293.71,  14243.28, 156752.62,
                               -429036.83, -171.91,     -118.50, 45.93,    -242239.29};
const double k54BothSums[25] = {1187784.03, 1172413.40, -137.78, -2596.33, 306558.60,
                                150151.70,  206.02,      -1045.45, -428548.81, 1597.75,
                                -84.17,     252.64,      1129.01, -356.66,  -1108.99,
                                -2264.25,   -969.75,     293.71,  14243.28, 156752.62,
                                -429036.83, -171.91,     -118.50, 45.93,    -242239.29};

/*! \brief 参考实现给出的期望值（tools/reference_parse.py，42 字节旧帧样例） */
const double kLegacyFrame0Raw[25] = {33.37, 33.40, 0.00, 0.00, 28.55, 28.74, -4.39, -3.20, 4.09,
                                     4.30,  -2.96, -1.11, -10.33, 11.51, 8.97, 0.03, 3.19, 0.05,
                                     0.00,  0.00,  0.00, 0.00,  0.00,  0.00, -66.38};
const double kLegacyFrame0Imu[25] = {23.04, 33.40, 0.00, 0.03, 40.06, 28.74, -4.39, -0.01, 13.06,
                                     4.30,  -2.96, -1.06, -10.33, 11.51, 8.97, 0.03, 3.19, 0.05,
                                     0.00,  0.00,  0.00, 0.00,  0.00,  0.00, -66.38};
const double kLegacyFrame1Imu[25] = {23.01, 33.37, 0.00, 0.00, 39.99, 28.46, -3.93, -0.05, 12.92,
                                     4.22,  -2.73, -0.96, -10.34, 11.66, 8.95, 0.00, 3.07, 0.19,
                                     0.00,  0.00,  0.00, 0.00,  0.00,  0.00, -65.14};
const double kLegacyFrame100Raw[25] = {27.93, 28.04, -1.07, -0.33, 22.68, 22.46, 0.54, -0.81, -11.70,
                                       -11.59, -3.88, -3.18, -5.17, 21.47, 1.93, 2.17, 0.90, -1.69,
                                       0.00,   0.00,  0.00,  0.00,  0.00,  0.00, -63.44};
const double kLegacyFrame100Imu[25] = {22.76, 28.04, -1.07, 1.84, 44.15, 22.46, 0.54, 0.09, -9.77,
                                       -11.59, -3.88, -4.87, -5.17, 21.47, 1.93, 2.17, 0.90, -1.69,
                                       0.00,   0.00,  0.00,  0.00,  0.00,  0.00, -63.44};
const double kLegacyFrame1303Raw[25] = {31.42, 31.35, 0.19, -0.24, 27.78, 27.94, -0.34, 0.59, -20.70,
                                        -20.72, -1.44, -1.03, 0.72, 20.45, -5.27, 0.86, -0.74, -0.40,
                                        0.00,   0.00,  0.00,  0.00,  0.00,  0.00, -62.94};
const double kLegacyFrame1303Imu[25] = {32.14, 31.35, 0.19, 0.62, 48.23, 27.94, -0.34, -0.15, -25.97,
                                        -20.72, -1.44, -1.43, 0.72, 20.45, -5.27, 0.86, -0.74, -0.40,
                                        0.00,   0.00,  0.00,  0.00,  0.00,  0.00, -62.94};

const double kLegacyRawSums[25] = {48997.70, 48880.83, -30.43, -969.50, 54018.22,
                                   53986.17, -176.43,  -349.13, -3140.20, -3230.96,
                                   -700.60,  -1435.91, 417.13, 3534.01,  454.05,
                                   594.27,   265.19,   10.41,  0.00,     0.00,
                                   0.00,     0.00,     0.00,   0.00,     -83290.69};
const double kLegacyImuSums[25] = {49414.83, 48880.83, -30.43, -375.23, 57552.23,
                                   53986.17, -176.43,  -83.94, -2686.15, -3230.96,
                                   -700.60,  -1425.50, 417.13, 3534.01,  454.05,
                                   594.27,   265.19,   10.41,  0.00,     0.00,
                                   0.00,     0.00,     0.00,   0.00,     -83290.69};

} // namespace

/*! \brief 解析核心单元测试 */
class ParserTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void protocolDefaults();
    void syntheticFrameRoundTrip();
    void syntheticCorrectionSourceModes();
    void syntheticCorrectionDisabled();
    void syntheticAngleWrap();
    void lins358NewFrameParsing();
    void mixedFrameLengthParsing();
    void frameSyncErrorAndResidual();
    void hexReaderFiltersWhitespace();
    void hexReaderRejectsIllegalChar();
    void hexReaderRejectsEmptyFile();
    void sampleFileStatistics();
    void sampleFileValues();
    void sampleFileColumnSums();
    void sampleFileCorrectionSources();
    void legacySampleFileStatistics();
    void legacySampleFileValues();
    void csvExportContent();
    void cancelDiscardsPartialData();
    void configMigrationFromLegacySchema();
    void configPersistenceRoundTrip();

private:
    std::unique_ptr<QTemporaryDir> m_settingsDir;
};

void ParserTest::initTestCase()
{
    // 配置测试必须与用户注册表/真实 INI 隔离：在 AppSettings 首次构造 QSettings 之前
    // 把默认格式切到 INI 并指向临时目录，结束后随临时目录一起删除。
    m_settingsDir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_settingsDir->isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir->path());
}

void ParserTest::cleanupTestCase()
{
    // 先清空静态 QSettings 中的数据，避免其析构时向已删除的临时目录回写。
    AppSettings::resetAll();
    m_settingsDir.reset();
}

void ParserTest::protocolDefaults()
{
    const FrameConfig cfg = FrameConfig::defaultConfig();
    QCOMPARE(cfg.frameLength, 54);
    QCOMPARE(cfg.legacyFrameLength, 42);
    QCOMPARE(cfg.header0, quint8(0xAA));
    QCOMPARE(cfg.header1, quint8(0x55));
    QCOMPARE(cfg.tail0, quint8(0x66));
    QCOMPARE(cfg.tail1, quint8(0xAA));
    QCOMPARE(cfg.fields.size(), 25);
    // 默认修正来源为 IMU5136，保持旧版行为
    QCOMPARE(cfg.correctionMode, CorrectionMode::ImuOnly);
    QVERIFY(cfg.insCorrectEnabled);

    // 新帧偏移连续 2+i*2；scale 统一 100
    for (int i = 0; i < cfg.fields.size(); ++i) {
        QCOMPARE(cfg.fields.at(i).offset, 2 + i * 2);
        QCOMPARE(cfg.fields.at(i).scale, 100.0);
    }
    // 前 12 个字段为机械轴量，其中 6 个参与惯导修正
    int corrected = 0;
    for (const FieldDef &f : cfg.fields) {
        if (f.correct != CorrectSource::None) {
            ++corrected;
            QVERIFY(f.mechAxis >= 0 && f.mechAxis <= 2);
        }
    }
    QCOMPARE(corrected, 6);
    // IMU5136 索引 12-17，LINS358 索引 18-23
    QCOMPARE(cfg.indexOf(QStringLiteral("GYRO_ANG_X")), 12);
    QCOMPARE(cfg.gyroAngleIndex(GyroX), 12);
    QCOMPARE(cfg.gyroRateIndex(GyroZ), 17);
    QCOMPARE(cfg.indexOf(QStringLiteral("LINS_ANG_X")), 18);
    QCOMPARE(cfg.linsAngleIndex(GyroX), 18);
    QCOMPARE(cfg.linsRateIndex(GyroZ), 23);
    QCOMPARE(cfg.indexOf(QStringLiteral("BEACON_PWR")), 24);
    // LINS358 六字段在旧帧中不存在（legacyOffset=-1）
    for (int i = 18; i <= 23; ++i)
        QCOMPARE(cfg.fields.at(i).legacyOffset, -1);
    // 信标在新帧偏移 50、旧帧偏移 38
    QCOMPARE(cfg.fields.at(24).offset, 50);
    QCOMPARE(cfg.fields.at(24).legacyOffset, 38);
    QVERIFY(cfg.isValid());

    // 非法修正来源 / 旧帧偏移越界必须被拒绝
    FrameConfig bad = cfg;
    bad.correctionMode = CorrectionMode(99);
    QVERIFY(!bad.isValid());
    bad = cfg;
    bad.fields[0].legacyOffset = 100;
    QVERIFY(!bad.isValid());
}

void ParserTest::syntheticFrameRoundTrip()
{
    const FrameConfig cfg = FrameConfig::defaultConfig();
    const QVector<int> values = {
        1000,  2000, -300, 400,  5000,  6000,  -700,
        800,   9000, 10000, -1100, 1200,
        130,   -140, 150,  -160, 170,  -180,   // IMU5136
        230,   -240, 250,  -260, 270,  -280,   // LINS358
        19000                                  // 信标
    };
    const QByteArray bytes = makeFrame(cfg, values) + makeFrame(cfg, values);

    DataSet data;
    FrameParser::Result result;
    QString error;
    QVERIFY2(FrameParser::parse(bytes, cfg, &data, &result, &error), qPrintable(error));
    QCOMPARE(result.stats.validFrames, qint64(2));
    QCOMPARE(result.stats.errorFrames, qint64(0));
    QCOMPARE(result.stats.residualBytes, qint64(0));
    QCOMPARE(result.stats.fieldCount, 25);
    QCOMPARE(data.frameCount(), 2);

    // 原始值 = 整数 / 100
    for (int f = 0; f < 25; ++f)
        QCOMPARE(data.rawValue(0, f), values.at(f) / 100.0);

    // 默认（仅 IMU5136）：目标角度 + 对应轴 IMU 角度，前馈速度 + 对应轴 IMU 速度。
    // IMU 角度 X/Y/Z = 1.30 / -1.40 / 1.50，IMU 速度 X/Y/Z = -1.60 / 1.70 / -1.80。
    QCOMPARE(data.value(0, 0), 10.00 + 1.30);     // 方位目标 + IMU 角度X
    QCOMPARE(data.value(0, 3), 4.00 + (-1.60));   // 方位前馈 + IMU 速度X
    QCOMPARE(data.value(0, 4), 50.00 + (-1.40));  // 俯仰目标 + IMU 角度Y
    QCOMPARE(data.value(0, 7), 8.00 + 1.70);      // 俯仰前馈 + IMU 速度Y
    QCOMPARE(data.value(0, 8), 90.00 + 1.50);     // 横滚目标 + IMU 角度Z
    QCOMPARE(data.value(0, 11), 12.00 + (-1.80)); // 横滚前馈 + IMU 速度Z
    // 未参与修正的字段保持原值
    QCOMPARE(data.value(0, 1), 20.00);
    QCOMPARE(data.value(0, 12), 1.30);
    // LINS358 字段自身是修正源（correct=None），保持原始值
    QCOMPARE(data.value(0, 18), 2.30);
    QCOMPARE(data.value(0, 19), -2.40);
    QCOMPARE(data.value(0, 23), -2.80);
    QCOMPARE(data.value(0, 24), 190.00); // 信标
}

/*!
 * \brief 修正来源三选一：IMU / LINS / 两者；并验证旧帧在 LINS 模式下回退 IMU
 *
 * 机械量原始值为 0；IMU 角度 X/Y/Z = 1/2/3°，LINS 角度 X/Y/Z = 10/20/30°；
 * IMU 速度全 0，LINS 速度 X/Y/Z = -1/-2/-3°/s。因此修正后的机械量可直接区分来源。
 */
void ParserTest::syntheticCorrectionSourceModes()
{
    QVector<int> values(25, 0);
    values[12] = 100;  // IMU 角度X = 1.00
    values[13] = 200;  // IMU 角度Y = 2.00
    values[14] = 300;  // IMU 角度Z = 3.00
    values[18] = 1000; // LINS 角度X = 10.00
    values[19] = 2000; // LINS 角度Y = 20.00
    values[20] = 3000; // LINS 角度Z = 30.00
    values[21] = -100; // LINS 速度X = -1.00
    values[22] = -200; // LINS 速度Y = -2.00
    values[23] = -300; // LINS 速度Z = -3.00

    FrameConfig cfg = FrameConfig::defaultConfig();
    const QByteArray newFrame = makeFrame(cfg, values);
    const QByteArray legacyFrame = makeLegacyFrame(cfg, values);

    const auto parseValue = [&](const QByteArray &bytes, CorrectionMode mode, int field) {
        cfg.correctionMode = mode;
        DataSet data;
        FrameParser::Result result;
        QString error;
        if (!FrameParser::parse(bytes, cfg, &data, &result, &error)) {
            qWarning("parse failed: %s", qPrintable(error));
            return qQNaN();
        }
        return data.value(0, field);
    };

    // IMU-only（默认）
    QCOMPARE(parseValue(newFrame, CorrectionMode::ImuOnly, 0), 1.00);
    QCOMPARE(parseValue(newFrame, CorrectionMode::ImuOnly, 3), 0.00);
    QCOMPARE(parseValue(newFrame, CorrectionMode::ImuOnly, 4), 2.00);
    QCOMPARE(parseValue(newFrame, CorrectionMode::ImuOnly, 8), 3.00);

    // LINS-only（新帧有 LINS358）
    QCOMPARE(parseValue(newFrame, CorrectionMode::LinsOnly, 0), 10.00);
    QCOMPARE(parseValue(newFrame, CorrectionMode::LinsOnly, 3), -1.00);
    QCOMPARE(parseValue(newFrame, CorrectionMode::LinsOnly, 4), 20.00);
    QCOMPARE(parseValue(newFrame, CorrectionMode::LinsOnly, 7), -2.00);
    QCOMPARE(parseValue(newFrame, CorrectionMode::LinsOnly, 8), 30.00);
    QCOMPARE(parseValue(newFrame, CorrectionMode::LinsOnly, 11), -3.00);

    // 两者叠加
    QCOMPARE(parseValue(newFrame, CorrectionMode::ImuAndLins, 0), 11.00);
    QCOMPARE(parseValue(newFrame, CorrectionMode::ImuAndLins, 3), -1.00);
    QCOMPARE(parseValue(newFrame, CorrectionMode::ImuAndLins, 4), 22.00);
    QCOMPARE(parseValue(newFrame, CorrectionMode::ImuAndLins, 7), -2.00);
    QCOMPARE(parseValue(newFrame, CorrectionMode::ImuAndLins, 8), 33.00);
    QCOMPARE(parseValue(newFrame, CorrectionMode::ImuAndLins, 11), -3.00);

    // 旧帧没有 LINS358：LINS-only 必须自动回退为 IMU5136
    QCOMPARE(parseValue(legacyFrame, CorrectionMode::LinsOnly, 0), 1.00);
    QCOMPARE(parseValue(legacyFrame, CorrectionMode::LinsOnly, 4), 2.00);
    QCOMPARE(parseValue(legacyFrame, CorrectionMode::LinsOnly, 8), 3.00);
    QCOMPARE(parseValue(legacyFrame, CorrectionMode::LinsOnly, 3), 0.00);
    QCOMPARE(parseValue(legacyFrame, CorrectionMode::LinsOnly, 11), 0.00);
    // 旧帧“两者”模式同理，只叠加存在的 IMU5136
    QCOMPARE(parseValue(legacyFrame, CorrectionMode::ImuAndLins, 0), 1.00);
    QCOMPARE(parseValue(legacyFrame, CorrectionMode::ImuAndLins, 8), 3.00);
}

void ParserTest::syntheticCorrectionDisabled()
{
    FrameConfig cfg = FrameConfig::defaultConfig();
    cfg.insCorrectEnabled = false;
    const FrameConfig &ccfg = cfg;
    const QVector<int> values = {
        1000, 2000, -300, 400, 5000, 6000, -700, 800, 9000, 10000,
        -1100, 1200,
        130, -140, 150, -160, 170, -180,
        230, -240, 250, -260, 270, -280,
        19000
    };
    const QByteArray bytes = makeFrame(ccfg, values);

    DataSet data;
    FrameParser::Result result;
    QString error;
    QVERIFY(FrameParser::parse(bytes, ccfg, &data, &result, &error));
    for (int f = 0; f < 25; ++f)
        QCOMPARE(data.value(0, f), data.rawValue(0, f));

    // 三轴独立开关：仅关闭横滚轴修正
    cfg.insCorrectEnabled = true;
    cfg.axisCorrectEnabled[MechRoll] = false;
    DataSet data2;
    FrameParser::Result result2;
    QVERIFY(FrameParser::parse(bytes, cfg, &data2, &result2, &error));
    QCOMPARE(data2.value(0, 0), 10.00 + 1.30); // 方位仍修正（默认 IMU）
    QCOMPARE(data2.value(0, 8), 90.00);        // 横滚目标未修正
    QCOMPARE(data2.value(0, 11), 12.00);       // 横滚前馈未修正
}

/*!
 * \brief 横滚角度环绕：0~180 保持，超过 180 减 360、显示为负值
 *
 * 覆盖字段 8（横滚目标角度）与字段 9（横滚编码器角度），含 0 / 180 / 180.01 / 360 等边界。
 */
void ParserTest::syntheticAngleWrap()
{
    struct Case {
        int target;        // 横滚目标角度原始整数
        int encoder;       // 横滚编码器角度原始整数
        double expTarget;  // 环绕后目标角度
        double expEncoder; // 环绕后编码器角度
    };
    const Case cases[] = {
        {0, 18000, 0.0, 180.0},   // 0° 保持；180° 保持正（未“超过”180）
        {18000, 18001, 180.0, -179.99}, // 180° 保持；180.01° → -179.99°
        {35000, 36000, -10.0, 0.0},     // 350° → -10°；360° → 0°
        {20000, 27000, -160.0, -90.0},  // 200° → -160°；270° → -90°
    };

    const FrameConfig cfg = FrameConfig::defaultConfig();
    QByteArray bytes;
    for (const Case &c : cases) {
        QVector<int> values(25, 0);
        values[8] = c.target;
        values[9] = c.encoder;
        bytes += makeFrame(cfg, values);
    }

    DataSet data;
    FrameParser::Result result;
    QString error;
    QVERIFY2(FrameParser::parse(bytes, cfg, &data, &result, &error), qPrintable(error));
    QCOMPARE(data.frameCount(), int(sizeof(cases) / sizeof(cases[0])));
    for (int i = 0; i < data.frameCount(); ++i) {
        QVERIFY(qAbs(data.rawValue(i, 8) - cases[i].expTarget) < 1e-9);
        QVERIFY(qAbs(data.rawValue(i, 9) - cases[i].expEncoder) < 1e-9);
    }
}

/*!
 * \brief LINS358 新帧解析：54 字节帧中字段 18-23 按偏移 38-49 解析
 *
 * LINS358 六字段始终保留在字段表中；修正时是否使用由 CorrectionMode 决定。
 */
void ParserTest::lins358NewFrameParsing()
{
    const FrameConfig cfg = FrameConfig::defaultConfig();
    QVector<int> values(25, 0);
    // IMU5136 角度 1.00/2.00/3.00
    values[12] = 100;
    values[13] = 200;
    values[14] = 300;
    // LINS358 角度 10.00/20.00/30.00，速度 -1.00/-2.00/-3.00
    values[18] = 1000;
    values[19] = 2000;
    values[20] = 3000;
    values[21] = -100;
    values[22] = -200;
    values[23] = -300;
    const QByteArray bytes = makeFrame(cfg, values);

    DataSet data;
    FrameParser::Result result;
    QString error;
    QVERIFY2(FrameParser::parse(bytes, cfg, &data, &result, &error), qPrintable(error));
    QCOMPARE(result.stats.validFrames, qint64(1));
    QCOMPARE(result.stats.fieldCount, 25);

    // LINS358 原始值正确
    QCOMPARE(data.rawValue(0, 18), 10.00);
    QCOMPARE(data.rawValue(0, 19), 20.00);
    QCOMPARE(data.rawValue(0, 20), 30.00);
    QCOMPARE(data.rawValue(0, 21), -1.00);
    QCOMPARE(data.rawValue(0, 22), -2.00);
    QCOMPARE(data.rawValue(0, 23), -3.00);
    // LINS358 字段自身是修正源（correct=None），其修正值与原始值一致
    for (int i = 18; i <= 23; ++i)
        QCOMPARE(data.value(0, i), data.rawValue(0, i));
    // 默认仅 IMU：机械字段只叠加 IMU5136
    QCOMPARE(data.value(0, 0), 1.00);
    QCOMPARE(data.value(0, 4), 2.00);
    QCOMPARE(data.value(0, 8), 3.00);
}

/*!
 * \brief 混合帧长自动识别：54 字节新帧与 42 字节旧帧交错排列时，解析器自动
 * 识别帧长；旧帧 LINS 字段补 0、信标按旧偏移 38 解析，新帧 LINS 正常解析。
 * 同时验证 LINS-only 模式对旧帧自动回退 IMU5136。
 */
void ParserTest::mixedFrameLengthParsing()
{
    FrameConfig cfg = FrameConfig::defaultConfig();
    QVector<int> newValues(25, 0);
    newValues[18] = 1000;  // 新帧 LINS 角度X = 10.00
    newValues[24] = -6600; // 新帧信标 -66.00（偏移 50）

    QVector<int> oldValues(25, 0);
    oldValues[12] = 500;   // 旧帧 IMU 角度X = 5.00
    oldValues[24] = -7000; // 旧帧信标 -70.00（偏移 38）

    const QByteArray newFrame = makeFrame(cfg, newValues);
    const QByteArray oldFrame = makeLegacyFrame(cfg, oldValues);
    const QByteArray bytes = newFrame + oldFrame + newFrame;

    DataSet data;
    FrameParser::Result result;
    QString error;
    QVERIFY2(FrameParser::parse(bytes, cfg, &data, &result, &error), qPrintable(error));
    QCOMPARE(result.stats.validFrames, qint64(3));
    QCOMPARE(result.stats.errorFrames, qint64(0));
    QCOMPARE(result.stats.residualBytes, qint64(0));

    // 帧0 新帧：LINS 角度X = 10.00，信标 -66.00
    QCOMPARE(data.rawValue(0, 18), 10.00);
    QCOMPARE(data.rawValue(0, 24), -66.00);
    // 帧1 旧帧：IMU 角度X = 5.00，LINS 补 0，信标 -70.00
    QCOMPARE(data.rawValue(1, 12), 5.00);
    QCOMPARE(data.rawValue(1, 18), 0.00);
    QCOMPARE(data.rawValue(1, 24), -70.00);
    // 帧2 新帧
    QCOMPARE(data.rawValue(2, 18), 10.00);
    QCOMPARE(data.rawValue(2, 24), -66.00);

    // 默认仅 IMU：新帧的 LINS 不参与修正（机械量原始值 0）
    QCOMPARE(data.value(0, 0), 0.00);
    QCOMPARE(data.value(1, 0), 5.00);

    // LINS-only：新帧用 LINS，旧帧回退 IMU
    cfg.correctionMode = CorrectionMode::LinsOnly;
    DataSet linsData;
    FrameParser::Result linsResult;
    QVERIFY2(FrameParser::parse(bytes, cfg, &linsData, &linsResult, &error), qPrintable(error));
    QCOMPARE(linsData.value(0, 0), 10.00);
    QCOMPARE(linsData.value(1, 0), 5.00);
    QCOMPARE(linsData.value(2, 0), 10.00);
}

void ParserTest::frameSyncErrorAndResidual()
{
    const FrameConfig cfg = FrameConfig::defaultConfig();
    const QVector<int> values(25, 0);
    const QByteArray good = makeFrame(cfg, values);

    // 帧尾被破坏 -> 计为错误帧；没有已接受帧时全部字节都算残帧
    QByteArray bad = good;
    bad[cfg.frameLength - 2] = char(0x00);
    bad[cfg.frameLength - 1] = char(0x00);

    DataSet data;
    FrameParser::Result result;
    QString error;
    QVERIFY(FrameParser::parse(bad, cfg, &data, &result, &error));
    QCOMPARE(result.stats.validFrames, qint64(0));
    QCOMPARE(result.stats.errorFrames, qint64(1));
    QCOMPARE(result.stats.residualBytes, qint64(54));

    // 有效帧 + 5 个尾部残字节
    QByteArray tail = good + QByteArray("\x01\x02\x03\x04\x05", 5);
    DataSet data2;
    FrameParser::Result result2;
    QVERIFY(FrameParser::parse(tail, cfg, &data2, &result2, &error));
    QCOMPARE(result2.stats.validFrames, qint64(1));
    QCOMPARE(result2.stats.errorFrames, qint64(0));
    QCOMPARE(result2.stats.residualBytes, qint64(5));

    // 前导干扰字节不影响同步（滑动窗口）
    QByteArray prefix = QByteArray("\x11\x22\xAA\x00", 4) + good + good;
    DataSet data3;
    FrameParser::Result result3;
    QVERIFY(FrameParser::parse(prefix, cfg, &data3, &result3, &error));
    QCOMPARE(result3.stats.validFrames, qint64(2));
    QCOMPARE(data3.frameCount(), 2);
}

void ParserTest::hexReaderFiltersWhitespace()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("ws.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("\xEF\xBB\xBF"); // UTF-8 BOM 应被跳过
    file.write("AA 55\n0F\tD3\r\n00 8C");
    file.close();

    const HexReadResult read = HexFileReader::readFile(path);
    QVERIFY2(read.ok, qPrintable(read.error));
    QCOMPARE(read.bytes.size(), 6);
    QCOMPARE(quint8(read.bytes.at(0)), quint8(0xAA));
    QCOMPARE(quint8(read.bytes.at(1)), quint8(0x55));
    QCOMPARE(quint8(read.bytes.at(2)), quint8(0x0F));
    QCOMPARE(quint8(read.bytes.at(5)), quint8(0x8C));
    QCOMPARE(read.charCount, qint64(12));
}

void ParserTest::hexReaderRejectsIllegalChar()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("bad.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("AA 55 ZZ");
    file.close();

    const HexReadResult read = HexFileReader::readFile(path);
    QVERIFY(!read.ok);
    QCOMPARE(read.errorOffset, qint64(6));
    QVERIFY(read.error.contains(QStringLiteral("非法")));
}

void ParserTest::hexReaderRejectsEmptyFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("empty.txt"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    const HexReadResult read = HexFileReader::readFile(path);
    QVERIFY(!read.ok);
    QCOMPARE(read.error, QStringLiteral("文件为空，无法解析"));
}

void ParserTest::sampleFileStatistics()
{
    const QString sample = locateSampleFile();
    if (sample.isEmpty())
        QSKIP("未找到 54 字节 sample 测试数据文件（可用环境变量 XW_SAMPLE 指定）");

    DataManager manager;
    QString error;
    QVERIFY2(manager.load(sample, FrameConfig::defaultConfig(), &error), qPrintable(error));

    const ParseStats &s = manager.stats();
    QCOMPARE(s.sourceBytes, qint64(626083));
    QCOMPARE(s.hexBytes, qint64(207414));
    QCOMPARE(s.validFrames, qint64(3841));
    QCOMPARE(s.errorFrames, qint64(0));
    QCOMPARE(s.residualBytes, qint64(0));
    QCOMPARE(s.fieldCount, 25);
    QCOMPARE(manager.data().frameCount(), 3841);
    QCOMPARE(manager.data().fieldCount(), 25);
    QVERIFY(s.errorRatio() < 0.10);
}

void ParserTest::sampleFileValues()
{
    const QString sample = locateSampleFile();
    if (sample.isEmpty())
        QSKIP("未找到 54 字节 sample 测试数据文件（可用环境变量 XW_SAMPLE 指定）");

    DataManager manager;
    QString error;
    QVERIFY(manager.load(sample, FrameConfig::defaultConfig(), &error));

    const DataSet &data = manager.data();
    const int frames[] = {0, 1, 100, 3840};
    const double *expectedRaw[] = {k54Frame0Raw, k54Frame1Raw, k54Frame100Raw, k54Frame3840Raw};
    const double *expectedImu[] = {k54Frame0Imu, k54Frame1Imu, k54Frame100Imu, k54Frame3840Imu};
    for (int i = 0; i < 4; ++i) {
        const int frame = frames[i];
        for (int f = 0; f < 25; ++f) {
            QCOMPARE(data.rawValue(frame, f), expectedRaw[i][f]);
            QCOMPARE(data.value(frame, f), expectedImu[i][f]);
        }
    }
}

void ParserTest::sampleFileColumnSums()
{
    const QString sample = locateSampleFile();
    if (sample.isEmpty())
        QSKIP("未找到 54 字节 sample 测试数据文件（可用环境变量 XW_SAMPLE 指定）");

    DataManager manager;
    QString error;
    QVERIFY(manager.load(sample, FrameConfig::defaultConfig(), &error));
    const DataSet &data = manager.data();
    for (int f = 0; f < 25; ++f) {
        QVERIFY(qAbs(data.columnSum(f, false) - k54RawSums[f]) < 0.5);
        QVERIFY(qAbs(data.columnSum(f, true) - k54ImuSums[f]) < 0.5);
    }
}

/*! \brief 真实 54 字节数据在三种修正来源下的关键字段值 */
void ParserTest::sampleFileCorrectionSources()
{
    const QString sample = locateSampleFile();
    if (sample.isEmpty())
        QSKIP("未找到 54 字节 sample 测试数据文件（可用环境变量 XW_SAMPLE 指定）");

    FrameConfig cfg = FrameConfig::defaultConfig();
    const auto firstFrameValues = [&](CorrectionMode mode) {
        cfg.correctionMode = mode;
        DataManager manager;
        QString error;
        if (!manager.load(sample, cfg, &error)) {
            qWarning("load failed: %s", qPrintable(error));
            return QVector<double>();
        }
        QVector<double> values;
        for (int f = 0; f < 25; ++f)
            values.append(manager.data().value(0, f));
        return values;
    };

    const QVector<double> imu = firstFrameValues(CorrectionMode::ImuOnly);
    const QVector<double> lins = firstFrameValues(CorrectionMode::LinsOnly);
    const QVector<double> both = firstFrameValues(CorrectionMode::ImuAndLins);
    QCOMPARE(imu.size(), 25);
    QCOMPARE(lins.size(), 25);
    QCOMPARE(both.size(), 25);

    // 方位目标 = 307.87 + {IMU -0.69 | LINS 2.97 | 两者}
    QCOMPARE(imu.at(0), 307.18);
    QCOMPARE(lins.at(0), 310.84);
    QCOMPARE(both.at(0), 310.15);
    // 俯仰目标 = 40.11 + {IMU 1.54 | LINS 41.22}
    QCOMPARE(imu.at(4), 41.65);
    QCOMPARE(lins.at(4), 81.33);
    QCOMPARE(both.at(4), 82.87);
    // 横滚目标 = 2.86 + {IMU -1.15 | LINS -113.23}
    QCOMPARE(imu.at(8), 1.71);
    QCOMPARE(lins.at(8), -110.37);
    QCOMPARE(both.at(8), -111.52);
    // 方位前馈 = 4.13 + {IMU -0.91 | LINS 0.03}
    QCOMPARE(imu.at(3), 3.22);
    QCOMPARE(lins.at(3), 4.16);
    QCOMPARE(both.at(3), 3.25);
    // 未参与修正的编码器角度三种模式一致
    QCOMPARE(imu.at(1), 307.86);
    QCOMPARE(lins.at(1), 307.86);
    QCOMPARE(both.at(1), 307.86);
}

void ParserTest::legacySampleFileStatistics()
{
    const QString sample = locateLegacySampleFile();
    if (sample.isEmpty())
        QSKIP("未找到 42 字节 sample 测试数据文件（可用环境变量 XW_SAMPLE_LEGACY 指定）");

    DataManager manager;
    QString error;
    QVERIFY2(manager.load(sample, FrameConfig::defaultConfig(), &error), qPrintable(error));

    const ParseStats &s = manager.stats();
    QCOMPARE(s.sourceBytes, qint64(164304));
    QCOMPARE(s.hexBytes, qint64(54768));
    QCOMPARE(s.validFrames, qint64(1304));
    QCOMPARE(s.errorFrames, qint64(0));
    QCOMPARE(s.residualBytes, qint64(0));
    QCOMPARE(s.fieldCount, 25);
    QCOMPARE(manager.data().frameCount(), 1304);
    QCOMPARE(manager.data().fieldCount(), 25);
}

void ParserTest::legacySampleFileValues()
{
    const QString sample = locateLegacySampleFile();
    if (sample.isEmpty())
        QSKIP("未找到 42 字节 sample 测试数据文件（可用环境变量 XW_SAMPLE_LEGACY 指定）");

    FrameConfig cfg = FrameConfig::defaultConfig();
    DataManager manager;
    QString error;
    QVERIFY(manager.load(sample, cfg, &error));

    const DataSet &data = manager.data();
    const int frames[] = {0, 1, 100, 1303};
    const double *expectedRaw[] = {kLegacyFrame0Raw, nullptr, kLegacyFrame100Raw,
                                   kLegacyFrame1303Raw};
    const double *expectedImu[] = {kLegacyFrame0Imu, kLegacyFrame1Imu, kLegacyFrame100Imu,
                                   kLegacyFrame1303Imu};
    for (int i = 0; i < 4; ++i) {
        const int frame = frames[i];
        for (int f = 0; f < 25; ++f) {
            if (expectedRaw[i])
                QCOMPARE(data.rawValue(frame, f), expectedRaw[i][f]);
            QCOMPARE(data.value(frame, f), expectedImu[i][f]);
        }
    }
    for (int f = 0; f < 25; ++f) {
        QVERIFY(qAbs(data.columnSum(f, false) - kLegacyRawSums[f]) < 0.5);
        QVERIFY(qAbs(data.columnSum(f, true) - kLegacyImuSums[f]) < 0.5);
    }

    // 旧帧没有 LINS358：LINS-only 必须与 IMU-only 结果完全一致
    cfg.correctionMode = CorrectionMode::LinsOnly;
    DataManager linsManager;
    QVERIFY(linsManager.load(sample, cfg, &error));
    const DataSet &lins = linsManager.data();
    for (int frame : {0, 1, 100, 1303}) {
        for (int f = 0; f < 25; ++f)
            QCOMPARE(lins.value(frame, f), data.value(frame, f));
    }
}

void ParserTest::csvExportContent()
{
    const QString sample = locateSampleFile();
    if (sample.isEmpty())
        QSKIP("未找到 54 字节 sample 测试数据文件（可用环境变量 XW_SAMPLE 指定）");

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    DataManager manager;
    QString error;
    QVERIFY(manager.load(sample, FrameConfig::defaultConfig(), &error));

    const QString csv = dir.filePath(QStringLiteral("out.csv"));
    QVERIFY2(manager.exportCsv(csv, &error), qPrintable(error));

    QFile file(csv);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray content = file.readAll();
    file.close();

    QVERIFY(content.startsWith("\xEF\xBB\xBF")); // UTF-8 BOM
    const QList<QByteArray> lines = content.split('\n');
    QCOMPARE(lines.size(), 3843);       // 1 表头 + 3841 数据 + 末尾空行
    QCOMPARE(lines.at(0).count(','), 31); // 帧序号 + 25 原始 + 6 修正

    const QList<QByteArray> header = lines.at(0).split(',');
    QCOMPARE(header.size(), 32);
    QVERIFY(header.at(0).endsWith(QByteArray("帧序号")));
    QVERIFY(header.at(1).contains(QByteArray("方位目标角度")));
    QVERIFY(header.at(13).contains(QByteArray("IMU5136角度X")));
    QVERIFY(header.at(19).contains(QByteArray("LINS358角度X")));

    const QList<QByteArray> first = lines.at(1).trimmed().split(',');
    QCOMPARE(first.size(), 32);
    QCOMPARE(first.at(0), QByteArray("0"));
    // 默认 IMU 修正来源；CSV 固定两位小数（业务要求）
    for (int f = 0; f < 25; ++f)
        QCOMPARE(first.at(1 + f), QByteArray::number(k54Frame0Raw[f], 'f', 2));
    const int correctedFields[6] = {0, 3, 4, 7, 8, 11};
    for (int i = 0; i < 6; ++i)
        QCOMPARE(first.at(26 + i), QByteArray::number(k54Frame0Imu[correctedFields[i]], 'f', 2));
}

/*!
 * \brief 回归测试：解析中途取消必须丢弃“半截数据”，并保留上一次成功的解析结果。
 *
 * 历史缺陷：FrameParser 取消后返回 true，DataManager 会把已解析的部分帧写入 m_data
 * 并返回成功，界面上就会出现“曲线只画了一半、统计却是完整文件”的误导现象。
 */
void ParserTest::cancelDiscardsPartialData()
{
    const QString sample = locateSampleFile();
    if (sample.isEmpty())
        QSKIP("未找到 54 字节 sample 测试数据文件（可用环境变量 XW_SAMPLE 指定）");

    const FrameConfig cfg = FrameConfig::defaultConfig();
    DataManager manager;
    QString error;
    QVERIFY2(manager.load(sample, cfg, &error), qPrintable(error));
    const qint64 fullFrames = manager.stats().validFrames;
    QCOMPARE(fullFrames, qint64(3841));

    // 百分比到 60% 时由用户“取消”
    int lastPercent = -1;
    const bool ok = manager.load(sample, cfg, &error, [&lastPercent](int percent) {
        lastPercent = percent;
        return percent < 60;
    });
    QVERIFY2(!ok, "取消后 load() 必须返回失败");
    QVERIFY(error.contains(QStringLiteral("取消")));
    QVERIFY(lastPercent >= 60);

    // 上一次的数据必须原样保留，不能被半截结果覆盖
    QCOMPARE(manager.stats().validFrames, fullFrames);
    QCOMPARE(manager.data().frameCount(), int(fullFrames));
}

/*!
 * \brief 配置迁移：旧版 42 字节 / 19 字段配置必须无损升级为 54/42 双帧长协议
 *
 * 关键点：旧配置里的信标偏移 38 会与新增的 LINS358 角度 X（38~39）重叠，
 * 若不迁移，整表校验失败会回退默认值，用户自定义的系数/颜色全部丢失。
 * 本用例必须在任何 AppSettings 读写之前运行，确保静态 QSettings 尚未缓存旧内容。
 */
void ParserTest::configMigrationFromLegacySchema()
{
    // 注意：这里不能先调用 AppSettings（其内部静态 QSettings 会在构造时读取空配置并缓存），
    // 必须先用独立 QSettings 写入旧版配置，再首次调用 loadFrameConfig 让静态实例读到旧数据。
    {
        QSettings s(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("XingWang"),
                    QStringLiteral("TurntableAnalyzer"));
        s.clear();
        s.setValue(QStringLiteral("protocol/frameLength"), 42);
        s.setValue(QStringLiteral("protocol/header0"), 0xAA);
        s.setValue(QStringLiteral("protocol/header1"), 0x55);
        s.setValue(QStringLiteral("protocol/tail0"), 0x66);
        s.setValue(QStringLiteral("protocol/tail1"), 0xAA);
        s.setValue(QStringLiteral("correct/enabled"), true);
        // 旧版没有 correct/source —— 应回退默认“仅 IMU5136”

        const QStringList oldKeys = {
            QStringLiteral("AZ_TARGET"),  QStringLiteral("AZ_ENCODER"),
            QStringLiteral("AZ_SPEED"),   QStringLiteral("AZ_FF"),
            QStringLiteral("EL_TARGET"),  QStringLiteral("EL_ENCODER"),
            QStringLiteral("EL_SPEED"),   QStringLiteral("EL_FF"),
            QStringLiteral("RL_TARGET"),  QStringLiteral("RL_ENCODER"),
            QStringLiteral("RL_SPEED"),   QStringLiteral("RL_FF"),
            QStringLiteral("GYRO_ANG_X"), QStringLiteral("GYRO_ANG_Y"),
            QStringLiteral("GYRO_ANG_Z"), QStringLiteral("GYRO_RATE_X"),
            QStringLiteral("GYRO_RATE_Y"),QStringLiteral("GYRO_RATE_Z"),
            QStringLiteral("BEACON_PWR")};
        QCOMPARE(oldKeys.size(), 19);

        s.beginWriteArray(QStringLiteral("protocol/fields"), oldKeys.size());
        for (int i = 0; i < oldKeys.size(); ++i) {
            s.setArrayIndex(i);
            s.setValue(QStringLiteral("key"), oldKeys.at(i));
            s.setValue(QStringLiteral("offset"), i == 18 ? 38 : 2 + i * 2);
            s.setValue(QStringLiteral("signed"), oldKeys.at(i) != QStringLiteral("BEACON_PWR"));
            // 自定义系数应被保留（AZ_TARGET 200）
            s.setValue(QStringLiteral("scale"),
                       oldKeys.at(i) == QStringLiteral("AZ_TARGET") ? 200.0 : 100.0);
            s.setValue(QStringLiteral("color"), QStringLiteral("#010203"));
        }
        s.endArray();
        s.sync();
    }

    const FrameConfig cfg = AppSettings::loadFrameConfig();
    QCOMPARE(cfg.frameLength, 54);
    QCOMPARE(cfg.legacyFrameLength, 42);
    QCOMPARE(cfg.fields.size(), 25);
    QCOMPARE(cfg.correctionMode, CorrectionMode::ImuOnly);
    QCOMPARE(cfg.fields.at(0).scale, 200.0); // 用户自定义系数保留
    QCOMPARE(cfg.fields.at(0).color, QColor(QStringLiteral("#010203")));
    // 信标：新帧偏移迁移到 50，旧帧仍为 38，类型修正为 Int16
    const int beacon = cfg.indexOf(QStringLiteral("BEACON_PWR"));
    QCOMPARE(beacon, 24);
    QCOMPARE(cfg.fields.at(beacon).offset, 50);
    QCOMPARE(cfg.fields.at(beacon).legacyOffset, 38);
    QCOMPARE(cfg.fields.at(beacon).kind, FieldKind::Int16);
    // LINS358 六字段使用默认偏移，旧帧标记为不存在
    for (int i = 18; i <= 23; ++i) {
        QCOMPARE(cfg.fields.at(i).offset, 2 + i * 2);
        QCOMPARE(cfg.fields.at(i).legacyOffset, -1);
    }
    QVERIFY(cfg.isValid());

    // 迁移结果必须写回为 v2 / 25 字段，避免下次启动重复迁移
    QSettings verify(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("XingWang"),
                     QStringLiteral("TurntableAnalyzer"));
    QCOMPARE(verify.value(QStringLiteral("protocol/configVersion")).toInt(), 2);
    QCOMPARE(verify.value(QStringLiteral("protocol/frameLength")).toInt(), 54);
    QCOMPARE(verify.beginReadArray(QStringLiteral("protocol/fields")), 25);
    verify.endArray();
}

/*! \brief 配置持久化：修正来源与全部字段元数据完整往返 */
void ParserTest::configPersistenceRoundTrip()
{
    AppSettings::resetAll();

    FrameConfig cfg = FrameConfig::defaultConfig();
    cfg.header0 = 0xA1;
    cfg.header1 = 0x5A;
    cfg.tail0 = 0x6B;
    cfg.tail1 = 0xB6;
    cfg.frameLength = 56;
    cfg.legacyFrameLength = 44;
    cfg.insCorrectEnabled = true;
    cfg.correctionMode = CorrectionMode::LinsOnly;
    cfg.axisCorrectEnabled[MechAzimuth] = true;
    cfg.axisCorrectEnabled[MechElevation] = false;
    cfg.axisCorrectEnabled[MechRoll] = true;
    cfg.gyroAxisForMech[MechAzimuth] = GyroZ;
    cfg.gyroAxisForMech[MechElevation] = GyroY;
    cfg.gyroAxisForMech[MechRoll] = GyroX;

    cfg.fields[0].name = QStringLiteral("自定义方位目标");
    cfg.fields[0].unit = QStringLiteral("mrad");
    cfg.fields[0].scale = 200.0;
    cfg.fields[0].kind = FieldKind::Int16;
    cfg.fields[0].wrap = AngleWrap::Signed180;
    cfg.fields[0].color = QColor(QStringLiteral("#123456"));
    cfg.fields[2].group = FieldGroup::Beacon;
    cfg.fields[2].legacyOffset = 6;

    AppSettings::saveFrameConfig(cfg);
    const FrameConfig loaded = AppSettings::loadFrameConfig();

    QCOMPARE(loaded.header0, quint8(0xA1));
    QCOMPARE(loaded.header1, quint8(0x5A));
    QCOMPARE(loaded.tail0, quint8(0x6B));
    QCOMPARE(loaded.tail1, quint8(0xB6));
    QCOMPARE(loaded.frameLength, 56);
    QCOMPARE(loaded.legacyFrameLength, 44);
    QCOMPARE(loaded.correctionMode, CorrectionMode::LinsOnly);
    QCOMPARE(loaded.axisCorrectEnabled[MechElevation], false);
    QCOMPARE(loaded.gyroAxisForMech[MechAzimuth], int(GyroZ));
    QCOMPARE(loaded.fields.size(), 25);
    for (int i = 0; i < 25; ++i)
        QCOMPARE(loaded.fields.at(i), cfg.fields.at(i));
    QVERIFY(loaded.isValid());

    // 再保存一次默认值，确认枚举 0 与默认配置也能正确覆盖旧值
    const FrameConfig defaults = FrameConfig::defaultConfig();
    AppSettings::saveFrameConfig(defaults);
    const FrameConfig reloaded = AppSettings::loadFrameConfig();
    QCOMPARE(reloaded.correctionMode, CorrectionMode::ImuOnly);
    QCOMPARE(reloaded.frameLength, 54);
    QCOMPARE(reloaded.legacyFrameLength, 42);
    for (int i = 0; i < 25; ++i)
        QCOMPARE(reloaded.fields.at(i), defaults.fields.at(i));
}

// 使用 QCoreApplication（无需 GUI 平台插件，便于 CI/命令行执行）
QTEST_GUILESS_MAIN(ParserTest)
#include "test_parser.moc"
