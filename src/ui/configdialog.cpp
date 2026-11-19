#include "configdialog.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <cmath>

namespace xn {

namespace {

/*! \brief 字段表格列序号（新增/调整列时只需改这里） */
enum FieldColumn {
    ColIndex = 0,
    ColName,
    ColKey,
    ColOffset,
    ColLegacyOffset,
    ColKind,
    ColScale,
    ColUnit,
    ColCorrect,
    ColColor,
    ColCount
};

} // namespace

ConfigDialog::ConfigDialog(const FrameConfig &frameConfig, const DisplayConfig &displayConfig,
                           QWidget *parent)
    : QDialog(parent)
    , m_cfg(frameConfig)
    , m_display(displayConfig)
{
    setWindowTitle(QStringLiteral("设置"));
    setMinimumSize(720, 520);

    auto *tabs = new QTabWidget(this);
    tabs->addTab(createProtocolTab(), QStringLiteral("帧协议"));
    tabs->addTab(createCorrectTab(), QStringLiteral("惯导修正"));
    tabs->addTab(createDisplayTab(), QStringLiteral("显示"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("确定"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("取消"));
    connect(buttons, &QDialogButtonBox::accepted, this, &ConfigDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ConfigDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addWidget(tabs, 1);
    root->addWidget(buttons);

    fillFieldTable();
}

QWidget *ConfigDialog::createProtocolTab()
{
    auto *page = new QWidget(this);
    auto *root = new QVBoxLayout(page);

    auto *frameBox = new QGroupBox(QStringLiteral("帧结构（大端序，高位在前）"), page);
    auto *form = new QFormLayout(frameBox);

    const auto makeHexEdit = [this](quint8 value) {
        auto *edit = new QLineEdit(QStringLiteral("%1").arg(value, 2, 16, QLatin1Char('0')).toUpper(),
                                   this);
        edit->setMaxLength(2);
        edit->setFixedWidth(56);
        return edit;
    };

    auto *headerRow = new QHBoxLayout;
    m_header0 = makeHexEdit(m_cfg.header0);
    m_header1 = makeHexEdit(m_cfg.header1);
    headerRow->addWidget(new QLabel(QStringLiteral("帧头 1："), this));
    headerRow->addWidget(m_header0);
    headerRow->addSpacing(12);
    headerRow->addWidget(new QLabel(QStringLiteral("帧头 2："), this));
    headerRow->addWidget(m_header1);
    headerRow->addStretch(1);
    form->addRow(QStringLiteral("帧头字节"), headerRow);

    auto *tailRow = new QHBoxLayout;
    m_tail0 = makeHexEdit(m_cfg.tail0);
    m_tail1 = makeHexEdit(m_cfg.tail1);
    tailRow->addWidget(new QLabel(QStringLiteral("帧尾 1："), this));
    tailRow->addWidget(m_tail0);
    tailRow->addSpacing(12);
    tailRow->addWidget(new QLabel(QStringLiteral("帧尾 2："), this));
    tailRow->addWidget(m_tail1);
    tailRow->addStretch(1);
    form->addRow(QStringLiteral("帧尾字节"), tailRow);

    m_frameLength = new QSpinBox(this);
    m_frameLength->setRange(4, 4096);
    m_frameLength->setValue(m_cfg.frameLength);
    m_frameLength->setSuffix(QStringLiteral(" 字节"));
    m_frameLength->setToolTip(QStringLiteral("新帧长度（54 字节，含 IMU5136 + LINS358）"));
    form->addRow(QStringLiteral("新帧长"), m_frameLength);

    m_legacyFrameLength = new QSpinBox(this);
    m_legacyFrameLength->setRange(4, 4096);
    m_legacyFrameLength->setValue(m_cfg.legacyFrameLength);
    m_legacyFrameLength->setSuffix(QStringLiteral(" 字节"));
    m_legacyFrameLength->setToolTip(
        QStringLiteral("旧帧长度（42 字节，仅含 IMU5136）；解析器会自动识别两种帧长"));
    form->addRow(QStringLiteral("旧帧长"), m_legacyFrameLength);

    auto *scaleRow = new QHBoxLayout;
    m_scaleAll = new QDoubleSpinBox(this);
    m_scaleAll->setRange(0.001, 100000.0);
    m_scaleAll->setDecimals(3);
    m_scaleAll->setValue(m_cfg.fields.isEmpty() ? 100.0 : m_cfg.fields.first().scale);
    auto *applyScale = new QPushButton(QStringLiteral("应用到全部字段"), this);
    connect(applyScale, &QPushButton::clicked, this, [this]() {
        const double scale = m_scaleAll->value();
        for (int row = 0; row < m_table->rowCount(); ++row) {
            if (QTableWidgetItem *item = m_table->item(row, 5))
                item->setText(QString::number(scale, 'f', 3));
        }
    });
    scaleRow->addWidget(m_scaleAll);
    scaleRow->addWidget(applyScale);
    scaleRow->addStretch(1);
    form->addRow(QStringLiteral("换算系数"), scaleRow);

    root->addWidget(frameBox);

    auto *tableBox =
        new QGroupBox(QStringLiteral("字段映射表（可修改新/旧帧偏移、换算系数与颜色）"), page);
    auto *tableLayout = new QVBoxLayout(tableBox);
    m_table = new QTableWidget(m_cfg.fields.size(), ColCount, tableBox);
    m_table->setHorizontalHeaderLabels(QStringList() << QStringLiteral("序号")
                                                     << QStringLiteral("中文名称")
                                                     << QStringLiteral("源码变量")
                                                     << QStringLiteral("新帧偏移")
                                                     << QStringLiteral("旧帧偏移")
                                                     << QStringLiteral("类型")
                                                     << QStringLiteral("系数")
                                                     << QStringLiteral("单位")
                                                     << QStringLiteral("惯导修正")
                                                     << QStringLiteral("颜色"));
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    // 双击颜色单元格弹出取色器
    connect(m_table, &QTableWidget::itemDoubleClicked, this, [this](QTableWidgetItem *item) {
        if (!item || item->column() != ColColor)
            return;
        const QColor current = item->data(Qt::UserRole).value<QColor>();
        const QColor chosen = QColorDialog::getColor(current.isValid() ? current : Qt::black, this,
                                                     QStringLiteral("选择曲线颜色"));
        if (chosen.isValid()) {
            item->setData(Qt::UserRole, chosen);
            item->setBackground(chosen);
            item->setToolTip(QStringLiteral("双击修改颜色"));
        }
    });
    tableLayout->addWidget(m_table);
    root->addWidget(tableBox, 1);

    return page;
}

QWidget *ConfigDialog::createCorrectTab()
{
    auto *page = new QWidget(this);
    auto *root = new QVBoxLayout(page);

    m_insEnabled = new QCheckBox(QStringLiteral("启用惯导修正（目标角度叠加惯导角度 / 前馈速度叠加惯导速度）"),
                                 page);
    m_insEnabled->setChecked(m_cfg.insCorrectEnabled);
    root->addWidget(m_insEnabled);

    auto *sourceBox = new QGroupBox(QStringLiteral("修正来源"), page);
    auto *sourceForm = new QFormLayout(sourceBox);
    m_correctMode = new QComboBox(sourceBox);
    m_correctMode->addItem(QStringLiteral("仅 IMU5136（默认，与旧版一致）"),
                           int(CorrectionMode::ImuOnly));
    m_correctMode->addItem(QStringLiteral("仅 LINS358（旧帧无 LINS 时自动回退 IMU）"),
                           int(CorrectionMode::LinsOnly));
    m_correctMode->addItem(QStringLiteral("IMU5136 + LINS358（两者叠加）"),
                           int(CorrectionMode::ImuAndLins));
    const int modeIndex = m_correctMode->findData(int(m_cfg.correctionMode));
    m_correctMode->setCurrentIndex(modeIndex >= 0 ? modeIndex : 0);
    m_correctMode->setToolTip(
        QStringLiteral("选择参与惯导角度/速度修正的数据源；两套惯导字段始终保留并显示。"));
    sourceForm->addRow(QStringLiteral("数据源"), m_correctMode);
    root->addWidget(sourceBox);

    auto *mapBox = new QGroupBox(QStringLiteral("惯导轴与机械轴对应关系"), page);
    auto *form = new QFormLayout(mapBox);
    const QString mechNames[3] = {QStringLiteral("方位轴"), QStringLiteral("俯仰轴"),
                                  QStringLiteral("横滚轴")};
    for (int a = 0; a < 3; ++a) {
        m_gyroForMech[a] = new QComboBox(mapBox);
        m_gyroForMech[a]->addItem(QStringLiteral("惯导 X"), int(GyroX));
        m_gyroForMech[a]->addItem(QStringLiteral("惯导 Y"), int(GyroY));
        m_gyroForMech[a]->addItem(QStringLiteral("惯导 Z"), int(GyroZ));
        m_gyroForMech[a]->setCurrentIndex(m_cfg.gyroAxisForMech[a]);
        form->addRow(mechNames[a], m_gyroForMech[a]);
    }
    root->addWidget(mapBox);

    auto *axisBox = new QGroupBox(QStringLiteral("三轴独立修正开关"), page);
    auto *axisLayout = new QHBoxLayout(axisBox);
    for (int a = 0; a < 3; ++a) {
        m_axisEnabled[a] = new QCheckBox(mechNames[a], axisBox);
        m_axisEnabled[a]->setChecked(m_cfg.axisCorrectEnabled[a]);
        axisLayout->addWidget(m_axisEnabled[a]);
    }
    axisLayout->addStretch(1);
    root->addWidget(axisBox);

    auto *tip = new QLabel(QStringLiteral("修正公式（按所选来源）：\n"
                                          "  仅 IMU：机械量 + IMU5136 对应轴角度/速度；\n"
                                          "  仅 LINS：机械量 + LINS358 对应轴角度/速度（旧帧或缺少 LINS 时回退 IMU）；\n"
                                          "  两者：机械量 + IMU5136 + LINS358。"),
                           page);
    tip->setStyleSheet(QStringLiteral("color:#555555;"));
    root->addWidget(tip);
    root->addStretch(1);
    return page;
}

QWidget *ConfigDialog::createDisplayTab()
{
    auto *page = new QWidget(this);
    auto *root = new QVBoxLayout(page);

    auto *box = new QGroupBox(QStringLiteral("绘图显示"), page);
    auto *form = new QFormLayout(box);

    m_lineWidth = new QDoubleSpinBox(box);
    m_lineWidth->setRange(0.2, 5.0);
    m_lineWidth->setSingleStep(0.2);
    m_lineWidth->setDecimals(1);
    m_lineWidth->setValue(m_display.lineWidth);
    form->addRow(QStringLiteral("曲线线宽"), m_lineWidth);

    m_grid = new QCheckBox(QStringLiteral("显示网格"), box);
    m_grid->setChecked(m_display.gridVisible);
    form->addRow(QString(), m_grid);

    m_subGrid = new QCheckBox(QStringLiteral("显示次网格"), box);
    m_subGrid->setChecked(m_display.subGridVisible);
    form->addRow(QString(), m_subGrid);

    m_antialias = new QCheckBox(QStringLiteral("启用抗锯齿"), box);
    m_antialias->setChecked(m_display.antialiasing);
    form->addRow(QString(), m_antialias);

    m_openGl = new QCheckBox(QStringLiteral("启用 OpenGL 加速（可选，部分显卡驱动下不稳定）"), box);
    m_openGl->setChecked(m_display.openGl);
    form->addRow(QString(), m_openGl);

    m_decimation = new QSpinBox(box);
    m_decimation->setRange(1000, 10000000);
    m_decimation->setSingleStep(1000);
    m_decimation->setValue(m_display.decimationThreshold);
    m_decimation->setSuffix(QStringLiteral(" 帧"));
    m_decimation->setToolTip(QStringLiteral("超过该帧数时启用绘制期自适应采样（只影响绘制速度，不改变数据）"));
    form->addRow(QStringLiteral("绘制自适应采样阈值"), m_decimation);

    m_maxPoints = new QSpinBox(box);
    m_maxPoints->setRange(1000, 200000);
    m_maxPoints->setSingleStep(1000);
    m_maxPoints->setValue(m_display.maxPointsPerCurve);
    m_maxPoints->setSuffix(QStringLiteral(" 点"));
    m_maxPoints->setToolTip(QStringLiteral(
        "每条曲线装入绘图控件的最大点数。绘图数据按当前可视窗口抽取，超出该值自动加大抽稀步长；\n"
        "绘图内存约为 25 × 该值 × 16 字节（默认 20000 → 约 8 MB），与文件大小无关。\n"
        "放大查看时窗口变小，步长自动恢复为 1（全分辨率）。"));
    form->addRow(QStringLiteral("每曲线最大点数"), m_maxPoints);

    root->addWidget(box);

    auto *pngBox = new QGroupBox(QStringLiteral("PNG 导出分辨率"), page);
    auto *pngForm = new QFormLayout(pngBox);
    m_pngWidth = new QSpinBox(pngBox);
    m_pngWidth->setRange(320, 10000);
    m_pngWidth->setValue(m_display.pngWidth);
    m_pngHeight = new QSpinBox(pngBox);
    m_pngHeight->setRange(240, 10000);
    m_pngHeight->setValue(m_display.pngHeight);
    pngForm->addRow(QStringLiteral("宽度"), m_pngWidth);
    pngForm->addRow(QStringLiteral("高度"), m_pngHeight);
    root->addWidget(pngBox);

    root->addStretch(1);
    return page;
}

void ConfigDialog::fillFieldTable()
{
    if (!m_table)
        return;
    for (int row = 0; row < m_cfg.fields.size(); ++row) {
        const FieldDef &f = m_cfg.fields.at(row);

        auto *index = new QTableWidgetItem(QString::number(row + 1));
        index->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(row, ColIndex, index);

        auto *name = new QTableWidgetItem(f.name);
        name->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(row, ColName, name);

        auto *key = new QTableWidgetItem(f.key);
        key->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(row, ColKey, key);

        m_table->setItem(row, ColOffset, new QTableWidgetItem(QString::number(f.offset)));

        auto *legacyOffset = new QTableWidgetItem(QString::number(f.legacyOffset));
        legacyOffset->setToolTip(
            QStringLiteral("旧帧（42 字节）中的字节偏移；-1 表示该字段在旧帧中不存在"));
        m_table->setItem(row, ColLegacyOffset, legacyOffset);

        auto *kind = new QTableWidgetItem(f.kind == FieldKind::Int16 ? QStringLiteral("int16")
                                                                     : QStringLiteral("uint16"));
        kind->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(row, ColKind, kind);

        m_table->setItem(row, ColScale, new QTableWidgetItem(QString::number(f.scale, 'f', 3)));

        auto *unit = new QTableWidgetItem(f.unit);
        unit->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(row, ColUnit, unit);

        QString correctText = QStringLiteral("不修正");
        if (f.correct == CorrectSource::GyroAngle)
            correctText = QStringLiteral("惯导角度修正");
        else if (f.correct == CorrectSource::GyroRate)
            correctText = QStringLiteral("惯导速度修正");
        auto *correct = new QTableWidgetItem(correctText);
        correct->setFlags(Qt::ItemIsEnabled);
        m_table->setItem(row, ColCorrect, correct);

        auto *color = new QTableWidgetItem;
        color->setFlags(Qt::ItemIsEnabled);
        color->setData(Qt::UserRole, f.color);
        color->setBackground(f.color);
        color->setToolTip(QStringLiteral("双击修改颜色"));
        m_table->setItem(row, ColColor, color);
    }
}

bool ConfigDialog::parseByte(const QLineEdit *edit, quint8 *out) const
{
    bool ok = false;
    const uint value = edit->text().trimmed().toUInt(&ok, 16);
    if (!ok || value > 0xFF)
        return false;
    *out = quint8(value);
    return true;
}

bool ConfigDialog::validateInputs(QString *why) const
{
    const struct {
        const QLineEdit *edit;
        const char *label;
    } hexEdits[] = {
        {m_header0, "帧头 1"}, {m_header1, "帧头 2"}, {m_tail0, "帧尾 1"}, {m_tail1, "帧尾 2"}};
    for (const auto &item : hexEdits) {
        quint8 value = 0;
        if (!item.edit || !parseByte(item.edit, &value)) {
            if (why) {
                *why = QStringLiteral("%1 必须是 00~FF 的两位十六进制数。")
                           .arg(QString::fromLatin1(item.label));
            }
            return false;
        }
    }

    if (!m_table)
        return true;
    const int newLen = m_frameLength->value();
    const int oldLen = m_legacyFrameLength->value();
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const QString rowName = QStringLiteral("第 %1 行").arg(row + 1);
        if (QTableWidgetItem *item = m_table->item(row, ColOffset)) {
            bool ok = false;
            const int offset = item->text().trimmed().toInt(&ok);
            if (!ok || offset < 2 || offset + 2 > newLen - 2) {
                if (why) {
                    *why = QStringLiteral("%1 的新帧偏移“%2”非法（合法范围 2~%3）。")
                               .arg(rowName, item->text().trimmed())
                               .arg(newLen - 4);
                }
                return false;
            }
        }
        if (QTableWidgetItem *item = m_table->item(row, ColLegacyOffset)) {
            bool ok = false;
            const int offset = item->text().trimmed().toInt(&ok);
            if (!ok || offset < -1 || (offset >= 0 && (offset < 2 || offset + 2 > oldLen - 2))) {
                if (why) {
                    *why = QStringLiteral(
                               "%1 的旧帧偏移“%2”非法（合法范围 2~%3，-1 表示旧帧无此字段）。")
                               .arg(rowName, item->text().trimmed())
                               .arg(oldLen - 4);
                }
                return false;
            }
        }
        if (QTableWidgetItem *item = m_table->item(row, ColScale)) {
            bool ok = false;
            const double scale = item->text().trimmed().toDouble(&ok);
            if (!ok || !(scale > 0.0) || !std::isfinite(scale)) {
                if (why) {
                    *why = QStringLiteral("%1 的换算系数“%2”必须是正的有限数。")
                               .arg(rowName, item->text().trimmed());
                }
                return false;
            }
        }
    }
    return true;
}

void ConfigDialog::applyTableToConfig(FrameConfig &cfg) const
{
    if (!m_table)
        return;
    for (int row = 0; row < cfg.fields.size() && row < m_table->rowCount(); ++row) {
        FieldDef &f = cfg.fields[row];
        if (QTableWidgetItem *item = m_table->item(row, ColOffset)) {
            bool ok = false;
            const int offset = item->text().toInt(&ok);
            if (ok)
                f.offset = offset;
        }
        if (QTableWidgetItem *item = m_table->item(row, ColLegacyOffset)) {
            bool ok = false;
            const int offset = item->text().toInt(&ok);
            if (ok)
                f.legacyOffset = offset;
        }
        if (QTableWidgetItem *item = m_table->item(row, ColKind)) {
            const QString kind = item->text().trimmed().toLower();
            f.kind = (kind == QStringLiteral("int16")) ? FieldKind::Int16 : FieldKind::UInt16;
        }
        if (QTableWidgetItem *item = m_table->item(row, ColScale)) {
            bool ok = false;
            const double scale = item->text().toDouble(&ok);
            if (ok && scale > 0.0)
                f.scale = scale;
        }
        if (QTableWidgetItem *item = m_table->item(row, ColColor)) {
            const QColor color = item->data(Qt::UserRole).value<QColor>();
            if (color.isValid())
                f.color = color;
        }
    }
}

FrameConfig ConfigDialog::frameConfig() const
{
    FrameConfig cfg = m_cfg;
    quint8 value = 0;
    if (parseByte(m_header0, &value))
        cfg.header0 = value;
    if (parseByte(m_header1, &value))
        cfg.header1 = value;
    if (parseByte(m_tail0, &value))
        cfg.tail0 = value;
    if (parseByte(m_tail1, &value))
        cfg.tail1 = value;
    cfg.frameLength = m_frameLength->value();
    cfg.legacyFrameLength = m_legacyFrameLength->value();
    cfg.insCorrectEnabled = m_insEnabled->isChecked();
    cfg.correctionMode =
        CorrectionMode(m_correctMode->currentData().toInt()); // 组合框保存的是枚举整数值
    for (int a = 0; a < 3; ++a) {
        cfg.gyroAxisForMech[a] = m_gyroForMech[a]->currentIndex();
        cfg.axisCorrectEnabled[a] = m_axisEnabled[a]->isChecked();
    }
    applyTableToConfig(cfg);
    return cfg;
}

DisplayConfig ConfigDialog::displayConfig() const
{
    DisplayConfig d = m_display;
    d.lineWidth = m_lineWidth->value();
    d.gridVisible = m_grid->isChecked();
    d.subGridVisible = m_subGrid->isChecked();
    d.antialiasing = m_antialias->isChecked();
    d.openGl = m_openGl->isChecked();
    d.decimationThreshold = m_decimation->value();
    d.maxPointsPerCurve = m_maxPoints->value();
    d.pngWidth = m_pngWidth->value();
    d.pngHeight = m_pngHeight->value();
    return d;
}

void ConfigDialog::accept()
{
    QString why;
    if (!validateInputs(&why)) {
        QMessageBox::warning(this, QStringLiteral("配置无效"), why);
        return;
    }
    const FrameConfig cfg = frameConfig();
    if (!cfg.isValid(&why)) {
        QMessageBox::warning(this, QStringLiteral("配置无效"), why);
        return;
    }
    if (cfg.frameLength != m_cfg.frameLength
        || cfg.legacyFrameLength != m_cfg.legacyFrameLength) {
        const auto answer = QMessageBox::question(
            this, QStringLiteral("确认修改"),
            QStringLiteral("帧长已修改为新帧 %1 / 旧帧 %2 字节，修改后需重新解析文件才能生效。"
                           "是否继续？")
                .arg(cfg.frameLength)
                .arg(cfg.legacyFrameLength));
        if (answer != QMessageBox::Yes)
            return;
    }
    m_cfg = cfg;
    m_display = displayConfig();
    QDialog::accept();
}

} // namespace xn
