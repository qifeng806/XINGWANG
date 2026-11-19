#ifndef XN_CONFIGDIALOG_H
#define XN_CONFIGDIALOG_H

/*!
 * \file configdialog.h
 * \brief 设置对话框：帧协议参数、惯导修正参数、显示参数。
 */

#include "appsettings.h"
#include "protocol.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QSpinBox;
class QTableWidget;

namespace xn {

/*! \brief 配置对话框 */
class ConfigDialog : public QDialog {
    Q_OBJECT
public:
    ConfigDialog(const FrameConfig &frameConfig, const DisplayConfig &displayConfig,
                 QWidget *parent = nullptr);

    /*! \brief 读取用户配置的帧协议 */
    FrameConfig frameConfig() const;
    /*! \brief 读取用户配置的显示参数 */
    DisplayConfig displayConfig() const;

protected:
    void accept() override;

private:
    QWidget *createProtocolTab();
    QWidget *createCorrectTab();
    QWidget *createDisplayTab();

    void fillFieldTable();
    void applyTableToConfig(FrameConfig &cfg) const;
    bool parseByte(const QLineEdit *edit, quint8 *out) const;
    /*! \brief 校验十六进制输入与字段表格（失败时通过 why 返回原因） */
    bool validateInputs(QString *why) const;

    FrameConfig m_cfg;
    DisplayConfig m_display;

    QLineEdit *m_header0 = nullptr;
    QLineEdit *m_header1 = nullptr;
    QLineEdit *m_tail0 = nullptr;
    QLineEdit *m_tail1 = nullptr;
    QSpinBox *m_frameLength = nullptr;
    QSpinBox *m_legacyFrameLength = nullptr;
    QDoubleSpinBox *m_scaleAll = nullptr;
    QTableWidget *m_table = nullptr;

    QCheckBox *m_insEnabled = nullptr;
    QComboBox *m_correctMode = nullptr;
    QComboBox *m_gyroForMech[3] = {nullptr, nullptr, nullptr};
    QCheckBox *m_axisEnabled[3] = {nullptr, nullptr, nullptr};

    QDoubleSpinBox *m_lineWidth = nullptr;
    QCheckBox *m_grid = nullptr;
    QCheckBox *m_subGrid = nullptr;
    QCheckBox *m_antialias = nullptr;
    QCheckBox *m_openGl = nullptr;
    QSpinBox *m_pngWidth = nullptr;
    QSpinBox *m_pngHeight = nullptr;
    QSpinBox *m_decimation = nullptr;
    QSpinBox *m_maxPoints = nullptr;
};

} // namespace xn

#endif // XN_CONFIGDIALOG_H
