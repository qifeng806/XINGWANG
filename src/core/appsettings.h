#ifndef XN_APPSETTINGS_H
#define XN_APPSETTINGS_H

/*!
 * \file appsettings.h
 * \brief 配置持久化（注册表 / ini）：帧协议参数、惯导修正参数、显示参数。
 */

#include "protocol.h"

#include <QString>

namespace xn {

/*! \brief 显示与绘图参数 */
struct DisplayConfig {
    double lineWidth = 1.0;         //!< 曲线线宽（px）
    bool gridVisible = true;        //!< 显示网格
    bool subGridVisible = true;     //!< 显示次网格
    bool antialiasing = true;       //!< 抗锯齿
    bool openGl = false;            //!< OpenGL 加速（可选）
    bool showRawValues = false;     //!< true=显示未修正原始值，false=显示惯导修正后的值
    bool showBias = false;          //!< true=在基础值上叠加手动偏差（显示偏差计算）
    int pngWidth = 1920;            //!< PNG 导出宽度
    int pngHeight = 1080;           //!< PNG 导出高度
    int decimationThreshold = 10000;//!< 超过该帧数启用绘制期自适应采样
    /*! \brief 每条曲线装载到绘图控件的最大点数
     *
     *  绘图数据按“当前可视窗口 + 余量”抽取，超出该点数时自动加大抽稀步长，
     *  从而把绘图侧内存限制在 曲线数 × 该值 × 16 字节（默认 25×20000×16 ≈ 8 MB），
     *  与文件大小无关；放大查看时窗口变小、步长自动恢复到 1（全分辨率）。
     */
    int maxPointsPerCurve = 20000;
};

/*! \brief 配置读写
 *
 *  \note 内部使用单个静态 QSettings 实例，**仅限 GUI 主线程调用**（QSettings 非线程安全）。
 *  \note 配置损坏的处理策略：加载时逐字段校验，非法字段忽略并回退默认值，
 *        随后**覆盖写回存储**（qWarning 记录原因），避免每次启动重复丢弃。
 */
class AppSettings {
public:
    static FrameConfig loadFrameConfig();
    static void saveFrameConfig(const FrameConfig &cfg);

    static DisplayConfig loadDisplayConfig();
    static void saveDisplayConfig(const DisplayConfig &cfg);

    /*! \brief 加载每个字段的“手动偏差”（默认全 0，长度按 fieldCount 对齐） */
    static QVector<double> loadFieldBias(int fieldCount);
    /*! \brief 保存每个字段的手动偏差（同一偏差行的字段取相同值） */
    static void saveFieldBias(const QVector<double> &bias);

    static QString lastDirectory();
    static void setLastDirectory(const QString &dir);

    /*! \brief 恢复出厂默认（清除全部配置项） */
    static void resetAll();
};

} // namespace xn

#endif // XN_APPSETTINGS_H
