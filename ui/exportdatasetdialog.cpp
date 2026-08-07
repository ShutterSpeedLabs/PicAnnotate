#include "exportdatasetdialog.h"

#include "../io/formatregistry.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

ExportDatasetDialog::ExportDatasetDialog(bool videoSource, QWidget *parent)
    : QDialog(parent)
    , m_videoSource(videoSource)
    , m_formatCombo(new QComboBox(this))
    , m_formatDescription(new QLabel(this))
    , m_taskCombo(new QComboBox(this))
    , m_outputEdit(new QLineEdit(this))
    , m_browseButton(new QPushButton(QStringLiteral("Browse..."), this))
    , m_copyImagesCheck(new QCheckBox(QStringLiteral("Copy image files into the dataset"), this))
    , m_includeEmptyCheck(new QCheckBox(QStringLiteral("Include frames with no annotations"), this))
    , m_valSplitSpin(new QDoubleSpinBox(this))
{
    setWindowTitle(QStringLiteral("Export Dataset"));

    for (const IDatasetFormat *format : FormatRegistry::exportFormats())
        m_formatCombo->addItem(format->displayName(), format->id());

    m_formatDescription->setWordWrap(true);
    m_formatDescription->setStyleSheet(QStringLiteral("color: gray;"));

    m_copyImagesCheck->setChecked(true);
    m_includeEmptyCheck->setToolTip(
        QStringLiteral("Emits background samples: an image entry with no objects."));

    m_valSplitSpin->setRange(0.0, 0.9);
    m_valSplitSpin->setSingleStep(0.05);
    m_valSplitSpin->setDecimals(2);
    m_valSplitSpin->setValue(0.0);
    m_valSplitSpin->setToolTip(
        QStringLiteral("Fraction of frames routed to a validation split. The split is "
                       "deterministic, so re-exporting gives the same result."));

    auto *outputRow = new QHBoxLayout;
    outputRow->addWidget(m_outputEdit);
    outputRow->addWidget(m_browseButton);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Format:"), m_formatCombo);
    form->addRow(QString(), m_formatDescription);
    form->addRow(QStringLiteral("Task:"), m_taskCombo);
    form->addRow(QStringLiteral("Output folder:"), outputRow);
    form->addRow(QStringLiteral("Validation split:"), m_valSplitSpin);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_exportButton = buttons->button(QDialogButtonBox::Ok);
    m_exportButton->setText(QStringLiteral("Export"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_copyImagesCheck);
    layout->addWidget(m_includeEmptyCheck);
    layout->addStretch();
    layout->addWidget(buttons);

    connect(m_formatCombo, &QComboBox::currentIndexChanged, this, &ExportDatasetDialog::onFormatChanged);
    connect(m_browseButton, &QPushButton::clicked, this, &ExportDatasetDialog::onBrowseClicked);
    connect(m_outputEdit, &QLineEdit::textChanged, this, &ExportDatasetDialog::validate);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    onFormatChanged();
    resize(520, sizeHint().height());
}

const IDatasetFormat *ExportDatasetDialog::selectedFormat() const
{
    return FormatRegistry::formatById(m_formatCombo->currentData().toString());
}

void ExportDatasetDialog::onFormatChanged()
{
    const IDatasetFormat *format = selectedFormat();
    if (!format)
        return;

    m_formatDescription->setText(format->description());

    // Rebuild the task list from the format's capabilities, keeping the previous
    // choice selected when the new format also supports it.
    const QVariant previous = m_taskCombo->currentData();
    m_taskCombo->clear();
    for (DatasetTask task : allDatasetTasks()) {
        if (format->supportsTask(task))
            m_taskCombo->addItem(datasetTaskDisplayName(task), static_cast<int>(task));
    }
    if (previous.isValid()) {
        const int index = m_taskCombo->findData(previous);
        if (index >= 0)
            m_taskCombo->setCurrentIndex(index);
    }

    // Video frames exist only in memory, so they must be written out.
    if (m_videoSource) {
        m_copyImagesCheck->setChecked(true);
        m_copyImagesCheck->setEnabled(false);
        m_copyImagesCheck->setToolTip(
            QStringLiteral("A video source has no image files to reference, so frames are always written."));
    }

    validate();
}

void ExportDatasetDialog::onBrowseClicked()
{
    const QString start = m_outputEdit->text().isEmpty() ? QDir::homePath() : m_outputEdit->text();
    const QString directory = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Select Export Folder"), start, QFileDialog::ShowDirsOnly);
    if (!directory.isEmpty())
        m_outputEdit->setText(directory);
}

void ExportDatasetDialog::validate()
{
    m_exportButton->setEnabled(!m_outputEdit->text().trimmed().isEmpty()
                               && m_taskCombo->count() > 0);
}

ExportOptions ExportDatasetDialog::options() const
{
    ExportOptions options;
    options.outputDir = m_outputEdit->text().trimmed();
    options.task = static_cast<DatasetTask>(m_taskCombo->currentData().toInt());
    options.copyImages = m_copyImagesCheck->isChecked();
    options.includeEmptyFrames = m_includeEmptyCheck->isChecked();
    options.valSplit = m_valSplitSpin->value();
    return options;
}
