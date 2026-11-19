#include "curvepanel.h"

#include "flowlayout.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpacerItem>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QVBoxLayout>
#include <cmath>

namespace xn {

namespace {

/*! \brief “已勾选数据”栏最多显示的行数（超出部分纵向滚动，避免挤占分组区） */
constexpr int kLegendMaxLines = 2;

/*! \brief 生成颜色块图标，作为复选框前置色标 */
QIcon colorIcon(const QColor &color)
{
    QPixmap pixmap(12, 12);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.fillRect(1, 1, 10, 10, color);
    painter.setPen(QColor(0x60, 0x60, 0x60));
    painter.drawRect(0, 0, 11, 11);
    painter.end();
    return QIcon(pixmap);
}

/*! \brief 面板分组定义：标题 + 每行“同一物理量的成对字段”key（行末共用一个偏差框） */
struct PanelGroupDef {
    QString title;
    QVector<QVector<QString>> rows;
};

QVector<QString> keyRow(const QString &a, const QString &b)
{
    QVector<QString> row;
    row << a << b;
    return row;
}

/*!
 * \brief 面板分组表
 *
 * 惯导按“角度 X/Y/Z”“速度 X/Y/Z”拆成两个分组框：原来 6 行的惯导分组框会把整个
 * 分组区顶高一倍（其它分组只有 3 行），行高参差且底部被滚动区裁掉。拆开后各分组框
 * 行数一致（信标 1 行），配合 FlowLayout 的行内等高，边框完全对齐。
 */
QVector<PanelGroupDef> panelGroupDefs()
{
    QVector<PanelGroupDef> groups;
    groups.append({QStringLiteral("角度类"),
                   {keyRow(QStringLiteral("AZ_TARGET"), QStringLiteral("AZ_ENCODER")),
                    keyRow(QStringLiteral("EL_TARGET"), QStringLiteral("EL_ENCODER")),
                    keyRow(QStringLiteral("RL_TARGET"), QStringLiteral("RL_ENCODER"))}});
    groups.append({QStringLiteral("速度类"),
                   {keyRow(QStringLiteral("AZ_SPEED"), QStringLiteral("AZ_FF")),
                    keyRow(QStringLiteral("EL_SPEED"), QStringLiteral("EL_FF")),
                    keyRow(QStringLiteral("RL_SPEED"), QStringLiteral("RL_FF"))}});
    groups.append({QStringLiteral("惯导类-角度"),
                   {keyRow(QStringLiteral("GYRO_ANG_X"), QStringLiteral("LINS_ANG_X")),
                    keyRow(QStringLiteral("GYRO_ANG_Y"), QStringLiteral("LINS_ANG_Y")),
                    keyRow(QStringLiteral("GYRO_ANG_Z"), QStringLiteral("LINS_ANG_Z"))}});
    groups.append({QStringLiteral("惯导类-速度"),
                   {keyRow(QStringLiteral("GYRO_RATE_X"), QStringLiteral("LINS_RATE_X")),
                    keyRow(QStringLiteral("GYRO_RATE_Y"), QStringLiteral("LINS_RATE_Y")),
                    keyRow(QStringLiteral("GYRO_RATE_Z"), QStringLiteral("LINS_RATE_Z"))}});
    QVector<QString> beaconRow;
    beaconRow << QStringLiteral("BEACON_PWR");
    groups.append({QStringLiteral("信标"), {beaconRow}});
    return groups;
}

/*!
 * \brief 偏差输入框的最小宽度
 *
 * 原先写死 74px：在 Windows 11 样式或高 DPI 下，样式自带的上下按钮本身就占掉大半宽度，
 * 数值被裁成“0.(”。这里按“最长可能数值文本 + 样式上下按钮 + 边框余量”动态计算，
 * 宽度随字体、样式与缩放自适应。
 */
int biasMinimumWidth(const QDoubleSpinBox *box)
{
    const QFontMetrics fm(box->fontMetrics());
    int textWidth = 0;
    for (double value : {box->minimum(), box->maximum(), 0.0}) {
        const QString text = QString::number(value, 'f', box->decimals());
        textWidth = qMax(textWidth, fm.horizontalAdvance(text));
    }

    QStyleOptionSpinBox option;
    option.initFrom(box);
    option.buttonSymbols = box->buttonSymbols();
    option.subControls = QStyle::SC_SpinBoxUp | QStyle::SC_SpinBoxDown | QStyle::SC_SpinBoxFrame;
    const int buttonWidth =
        qMax(0, box->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxUp, box).width());
    const int frameWidth = box->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, nullptr, box);

    return qMax(74, textWidth + 2 * buttonWidth + 2 * frameWidth + 12);
}

} // namespace

CurvePanel::CurvePanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("curvePanel"));
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(6, 4, 6, 4);
    m_rootLayout->setSpacing(4);
}

void CurvePanel::build(const FrameConfig &cfg)
{
    m_cfg = cfg;
    m_boxes.clear();
    m_boxes.resize(cfg.fields.size()); // 按字段序号索引，未创建的项保持为空指针
    m_biasEdits.clear();
    m_biasEdits.resize(cfg.fields.size()); // 同一偏差行的字段指向同一个输入框
    m_legendScroll = nullptr;
    m_legendFlow = nullptr;
    m_legendPlaceholder = nullptr;
    m_legendItems.clear();
    m_scroll = nullptr;
    m_presetLayout = nullptr;
    m_groupsFlow = nullptr;

    // 整体重建：删除旧的内容容器（预设行、已勾选栏、分组滚动区都在其中）
    if (m_content) {
        m_rootLayout->removeWidget(m_content);
        delete m_content;
        m_content = nullptr;
    }

    m_content = new QWidget(this);
    auto *root = new QVBoxLayout(m_content);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    // ── 常用对比预设 ──────────────────────────────────────────────
    auto *presetRow = new QHBoxLayout;
    presetRow->setSpacing(6);
    presetRow->addWidget(new QLabel(QStringLiteral("常用对比："), m_content));
    struct PresetButton {
        const char *text;
        int preset;
    };
    const PresetButton presets[] = {
        {"角度对比", 0}, {"速度对比", 1}, {"惯导对比", 2}, {"全量显示", 3}, {"清空全部", 4}};
    for (const PresetButton &p : presets) {
        auto *button = new QPushButton(QString::fromUtf8(p.text), m_content);
        button->setMinimumWidth(84);
        const int preset = p.preset;
        connect(button, &QPushButton::clicked, this, [this, preset]() { emit presetRequested(preset); });
        presetRow->addWidget(button);
    }
    presetRow->addStretch(1);
    m_presetLayout = presetRow;
    root->addLayout(presetRow);

    // ── 已勾选数据名称标签栏 ──────────────────────────────────────
    // 每个字段一个“色块 + 名称”小标签，用 FlowLayout 排列：换行由布局计算（不再依赖
    // QLabel 富文本换行——它在 QScrollArea 内的理想高度不可靠，会导致文字被截断），
    // 勾选变化时只需显隐对应标签，无需重排文本。
    auto *checkedRow = new QHBoxLayout;
    checkedRow->setSpacing(6);
    checkedRow->addWidget(new QLabel(QStringLiteral("已勾选数据："), m_content));

    auto *legend = new QScrollArea(m_content);
    legend->setWidgetResizable(true);
    legend->setFrameShape(QFrame::StyledPanel);
    legend->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // 换行由 FlowLayout 负责

    auto *legendContent = new QWidget(legend);
    auto *legendFlow = new FlowLayout(legendContent, 0, 8, 2);
    legendFlow->setJustify(false);       // 标签行不拉伸，自然左对齐
    legendFlow->setUniformHeight(true);
    m_legendItems.clear();
    m_legendItems.resize(m_cfg.fields.size());
    for (int f = 0; f < m_cfg.fields.size(); ++f) {
        const FieldDef &def = m_cfg.fields.at(f);
        auto *chip = new QLabel(legendContent);
        chip->setTextFormat(Qt::RichText);
        chip->setText(QStringLiteral("<span style=\"color:%1\">&#9632;</span>&nbsp;%2")
                          .arg(def.color.name(), def.name.toHtmlEscaped()));
        chip->setToolTip(QStringLiteral("%1（%2）\n单位：%3  帧内偏移：%4  类型：%5  系数：÷%6")
                             .arg(def.name, def.key, def.unit)
                             .arg(def.offset)
                             .arg(def.kind == FieldKind::Int16 ? QStringLiteral("int16")
                                                               : QStringLiteral("uint16"))
                             .arg(def.scale));
        chip->setVisible(true); // 新建的复选框默认全部勾选
        legendFlow->addWidget(chip);
        m_legendItems[f] = chip;
    }
    m_legendPlaceholder = new QLabel(QStringLiteral("<i>（未勾选任何数据）</i>"), legendContent);
    m_legendPlaceholder->setTextFormat(Qt::RichText);
    m_legendPlaceholder->setVisible(false);
    legendFlow->addWidget(m_legendPlaceholder);
    m_legendFlow = legendFlow;

    legend->setWidget(legendContent);
    checkedRow->addWidget(legend, 1);
    m_legendScroll = legend;
    root->addLayout(checkedRow);

    // ── 分组复选框（独立滚动区，内部 FlowLayout 直接作为容器主布局） ──
    m_scroll = new QScrollArea(m_content);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setMinimumHeight(120); // 至少露出一行分组框，避免被预设行/已勾选栏挤成一条缝
    // 分组容器主布局为 FlowLayout（支持 heightForWidth）：宽度足够时横向排列，不足时
    // 自动换行并铺满整行；高度由 FlowLayout 按宽度计算，QScrollArea 据此垂直滚动。
    auto *groupsContent = new QWidget(m_scroll);
    auto *groupsFlow = new FlowLayout(groupsContent, 0, 8, 8);
    groupsFlow->setJustify(true);
    groupsFlow->setUniformHeight(true);
    m_groupsFlow = groupsFlow;
    for (const PanelGroupDef &group : panelGroupDefs()) {
        if (QGroupBox *box = createGroupBox(group.title, group.rows, groupsContent))
            groupsFlow->addWidget(box);
    }
    m_scroll->setWidget(groupsContent);
    root->addWidget(m_scroll, 1); // 分组滚动区占满面板剩余空间

    m_rootLayout->addWidget(m_content);
    refreshCheckedLabel();
    updateLegendHeight();
}

QGroupBox *CurvePanel::createGroupBox(const QString &title, const QVector<QVector<QString>> &rows,
                                      QWidget *parent)
{
    auto *box = new QGroupBox(title, parent);
    auto *grid = new QGridLayout(box);
    grid->setContentsMargins(8, 4, 8, 6);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(4);

    // 按 key 解析每行的字段序号（不依赖字段在配置中的排列顺序）
    QVector<QVector<int>> rowFields;
    int dataColumns = 0;
    for (const QVector<QString> &keys : rows) {
        QVector<int> indices;
        for (const QString &key : keys) {
            for (int f = 0; f < m_cfg.fields.size(); ++f) {
                if (m_cfg.fields.at(f).key == key) {
                    indices.append(f);
                    break;
                }
            }
        }
        if (indices.isEmpty())
            continue;
        dataColumns = qMax(dataColumns, indices.size());
        rowFields.append(indices);
    }
    if (rowFields.isEmpty()) {
        delete box;
        return nullptr;
    }

    const int biasColumn = dataColumns; //!< 偏差列紧随数据列（列号不再写死为 2）

    // 偏差列表头（复选框列不设表头，数据从第 1 行开始）
    auto *biasHeader = new QLabel(QStringLiteral("偏差"), box);
    biasHeader->setAlignment(Qt::AlignCenter);
    biasHeader->setStyleSheet(QStringLiteral("color:#666;"));
    grid->addWidget(biasHeader, 0, biasColumn);

    // 按“偏差行”构建：每行同一物理量的若干字段 + 一个共用偏差框
    for (int r = 0; r < rowFields.size(); ++r) {
        const QVector<int> &fieldIndices = rowFields.at(r);
        const int row = r + 1; // 第 0 行是表头

        for (int c = 0; c < fieldIndices.size(); ++c) {
            const int fieldIndex = fieldIndices.at(c);
            const FieldDef &def = m_cfg.fields.at(fieldIndex);
            auto *check = new QCheckBox(def.name, box);
            check->setChecked(true);
            check->setIcon(colorIcon(def.color));
            check->setToolTip(QStringLiteral("%1（%2）\n帧内偏移：%3  类型：%4  系数：÷%5")
                                  .arg(def.name, def.key)
                                  .arg(def.offset)
                                  .arg(def.kind == FieldKind::Int16 ? QStringLiteral("int16")
                                                                    : QStringLiteral("uint16"))
                                  .arg(def.scale));
            // 关键：复选框必须按“字段序号”存放，界面按分组排列，两者顺序不同。
            connect(check, &QCheckBox::toggled, this, [this, fieldIndex](bool checked) {
                refreshCheckedLabel();
                emit curveToggled(fieldIndex, checked);
            });
            grid->addWidget(check, row, c);
            m_boxes[fieldIndex] = check;
        }

        // 行末偏差输入框：一个值同时加到该行所有字段
        auto *bias = new QDoubleSpinBox(box);
        bias->setRange(-100000.0, 100000.0);
        bias->setDecimals(2);
        bias->setSingleStep(1.0);
        bias->setValue(0.0);
        bias->setMinimumWidth(biasMinimumWidth(bias));
        bias->setAlignment(Qt::AlignRight);
        bias->setAccelerated(true);
        bias->setToolTip(QStringLiteral("手动偏差：叠加到本行 %1 条曲线（如 +10 或 -10），\n"
                                       "输入后自动保存，需配合工具栏“显示偏差计算”查看")
                            .arg(fieldIndices.size()));
        connect(bias, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this, fieldIndices](double v) { emit fieldBiasChanged(fieldIndices, v); });
        grid->addWidget(bias, row, biasColumn);
        for (int fi : fieldIndices)
            m_biasEdits[fi] = bias;
    }

    // 多余宽度分摊到数据列：偏差列保持自然宽度并紧贴分组框右侧，标签列均匀铺开
    for (int c = 0; c < dataColumns; ++c)
        grid->setColumnStretch(c, 1);

    // 底部占位行：FlowLayout 把本分组框拉高到与同行其它分组框等高时，
    // 多余高度全部落在最下方，行距不被撑开（否则行间距会随窗口变大而变松）。
    grid->addItem(new QSpacerItem(0, 0, QSizePolicy::Minimum, QSizePolicy::Expanding),
                  rowFields.size() + 1, 0);

    return box;
}

void CurvePanel::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateLegendHeight(); // 宽度变化会改变标签换行行数
}

int CurvePanel::preferredHeight(int width) const
{
    if (!m_content || !m_groupsFlow || !m_rootLayout)
        return minimumSizeHint().height();

    const QMargins margins = m_rootLayout->contentsMargins();
    const int spacing = m_rootLayout->spacing();
    int height = margins.top() + margins.bottom();
    int blocks = 0;

    if (m_presetLayout) {
        height += m_presetLayout->sizeHint().height();
        ++blocks;
    }
    if (m_legendScroll) {
        const int oneLine = fontMetrics().height() + 2;
        height += qMax(m_legendScroll->height(), oneLine);
        ++blocks;
    }
    if (m_scroll) {
        // 分组区宽度 = 面板宽度 - 边距 - 滚动区边框与纵向滚动条
        const int inner = qMax(160, width - margins.left() - margins.right()
                                        - 2 * m_scroll->frameWidth() - 18);
        height += qMax(m_scroll->minimumHeight(), m_groupsFlow->heightForWidth(inner));
        ++blocks;
    }
    if (blocks > 1)
        height += spacing * (blocks - 1);
    return height;
}

void CurvePanel::updateLegendHeight()
{
    if (!m_legendScroll || !m_legendFlow)
        return;

    // 单行高度 = 最高的可见标签；空列表时用占位标签兜底
    int chipHeight = 0;
    for (QLabel *chip : m_legendItems) {
        if (chip && chip->isVisible())
            chipHeight = qMax(chipHeight, chip->sizeHint().height());
    }
    if (m_legendPlaceholder && m_legendPlaceholder->isVisible())
        chipHeight = qMax(chipHeight, m_legendPlaceholder->sizeHint().height());
    if (chipHeight <= 0)
        chipHeight = fontMetrics().height() + 2;

    const int available = m_legendScroll->viewport()->width();
    const int content = m_legendFlow->heightForWidth(qMax(40, available - 2));
    const int maxVisible = kLegendMaxLines * chipHeight + (kLegendMaxLines - 1) * m_legendFlow->verticalSpacing();
    // 固定高度是滚动区外框尺寸，需加上边框宽度，否则视口比内容少 2px 而多出滚动条
    const int visible = qBound(chipHeight, content, maxVisible) + 2 * m_legendScroll->frameWidth();
    if (m_legendScroll->minimumHeight() == visible && m_legendScroll->maximumHeight() == visible)
        return;
    m_legendScroll->setFixedHeight(visible);
}

void CurvePanel::setCheckedSilently(int fieldIndex, bool checked)
{
    if (fieldIndex < 0 || fieldIndex >= m_boxes.size())
        return;
    QCheckBox *box = m_boxes.at(fieldIndex);
    if (!box || box->isChecked() == checked)
        return;
    const QSignalBlocker blocker(box);
    box->setChecked(checked);
    if (!m_batchUpdating)
        refreshCheckedLabel();
}

void CurvePanel::setBiasSilently(int fieldIndex, double value)
{
    if (fieldIndex < 0 || fieldIndex >= m_biasEdits.size())
        return;
    QDoubleSpinBox *edit = m_biasEdits.at(fieldIndex);
    if (!edit || !std::isfinite(value))
        return;
    // 同一偏差行的多个字段指向同一输入框，回填一次即可
    if (edit->value() != value) {
        const QSignalBlocker blocker(edit);
        edit->setValue(value);
    }
}

void CurvePanel::endBatchUpdate()
{
    m_batchUpdating = false;
    refreshCheckedLabel();
}

void CurvePanel::setAllCheckedSilently(bool checked)
{
    for (QCheckBox *box : m_boxes) {
        if (!box)
            continue;
        const QSignalBlocker blocker(box);
        box->setChecked(checked);
    }
    refreshCheckedLabel();
}

bool CurvePanel::isChecked(int fieldIndex) const
{
    if (fieldIndex < 0 || fieldIndex >= m_boxes.size() || !m_boxes.at(fieldIndex))
        return false;
    return m_boxes.at(fieldIndex)->isChecked();
}

void CurvePanel::refreshCheckedLabel()
{
    int checked = 0;
    for (int f = 0; f < m_legendItems.size() && f < m_boxes.size(); ++f) {
        QCheckBox *box = m_boxes.at(f);
        QLabel *chip = m_legendItems.at(f);
        if (!chip)
            continue;
        const bool visible = box && box->isChecked();
        if (chip->isVisible() != visible)
            chip->setVisible(visible);
        if (visible)
            ++checked;
    }
    if (m_legendPlaceholder && m_legendPlaceholder->isVisible() != (checked == 0))
        m_legendPlaceholder->setVisible(checked == 0);

    updateLegendHeight();
    if (m_legendScroll)
        m_legendScroll->verticalScrollBar()->setValue(0); // 始终从第一条开始显示
}

} // namespace xn
