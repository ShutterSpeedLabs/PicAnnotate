#include "importdatasetdialog.h"

#include "../io/formatregistry.h"
#include "recentpaths.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

ImportDatasetDialog::ImportDatasetDialog(QWidget *parent)
    : QDialog(parent)
    , m_formatCombo(new QComboBox(this))
    , m_formatDescription(new QLabel(this))
    , m_pathLabel(new QLabel(this))
    , m_pathEdit(new QLineEdit(this))
    , m_browseButton(new QPushButton(QStringLiteral("Browse..."), this))
    , m_replaceCheck(new QCheckBox(QStringLiteral("Replace the project's current annotations"), this))
    , m_groupTracksCheck(new QCheckBox(QStringLiteral("Group annotations sharing a track id into tracks"), this))
{
    setWindowTitle(QStringLiteral("Import Dataset"));

    for (const IDatasetFormat *format : FormatRegistry::importFormats())
        m_formatCombo->addItem(format->displayName(), format->id());

    m_formatDescription->setWordWrap(true);
    m_formatDescription->setStyleSheet(QStringLiteral("color: gray;"));
    m_replaceCheck->setChecked(true);
    m_groupTracksCheck->setChecked(true);
    m_groupTracksCheck->setToolTip(
        QStringLiteral("Video datasets carry a track id per object. Grouping restores them as "
                       "tracks with keyframes; leaving it off imports every annotation as a "
                       "separate per-frame shape. Datasets without track ids are unaffected."));

    auto *pathRow = new QHBoxLayout;
    pathRow->addWidget(m_pathEdit);
    pathRow->addWidget(m_browseButton);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Format:"), m_formatCombo);
    form->addRow(QString(), m_formatDescription);
    form->addRow(m_pathLabel, pathRow);

    auto *hint = new QLabel(
        QStringLiteral("Imported classes are merged into the project's label list by name. "
                       "If the dataset's images cannot be found, open the folder containing "
                       "them first and the annotations will be matched by file name."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: gray;"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_importButton = buttons->button(QDialogButtonBox::Ok);
    m_importButton->setText(QStringLiteral("Import"));

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_replaceCheck);
    layout->addWidget(m_groupTracksCheck);
    layout->addWidget(hint);
    layout->addStretch();
    layout->addWidget(buttons);

    connect(m_formatCombo, &QComboBox::currentIndexChanged, this, &ImportDatasetDialog::onFormatChanged);
    connect(m_browseButton, &QPushButton::clicked, this, &ImportDatasetDialog::onBrowseClicked);
    connect(m_pathEdit, &QLineEdit::textChanged, this, &ImportDatasetDialog::validate);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    onFormatChanged();
    resize(560, sizeHint().height());
}

const IDatasetFormat *ImportDatasetDialog::selectedFormat() const
{
    return FormatRegistry::formatById(m_formatCombo->currentData().toString());
}

void ImportDatasetDialog::onFormatChanged()
{
    const IDatasetFormat *format = selectedFormat();
    if (!format)
        return;

    m_formatDescription->setText(format->description());
    // Some formats are a single annotation file, others a directory tree; say
    // which one the path field wants.
    m_pathLabel->setText(format->importsDirectory() ? QStringLiteral("Dataset folder:")
                                                    : QStringLiteral("Annotation file:"));
    m_pathEdit->clear();
    validate();
}

void ImportDatasetDialog::onBrowseClicked()
{
    const IDatasetFormat *format = selectedFormat();
    if (!format)
        return;

    const QString start = m_pathEdit->text().isEmpty() ? RecentPaths::dir(RecentPaths::Datasets)
                                                       : m_pathEdit->text();

    QString chosen;
    if (format->importsDirectory()) {
        chosen = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Dataset Folder"),
                                                   start, QFileDialog::ShowDirsOnly);
    } else {
        chosen = QFileDialog::getOpenFileName(this, QStringLiteral("Select Annotation File"),
                                              start, format->importFileFilter());
    }

    if (!chosen.isEmpty()) {
        m_pathEdit->setText(chosen);
        RecentPaths::remember(RecentPaths::Datasets, chosen);
    }
}

void ImportDatasetDialog::validate()
{
    m_importButton->setEnabled(!m_pathEdit->text().trimmed().isEmpty());
}

ImportOptions ImportDatasetDialog::options() const
{
    ImportOptions options;
    options.path = m_pathEdit->text().trimmed();
    options.replaceExisting = m_replaceCheck->isChecked();
    options.groupTracks = m_groupTracksCheck->isChecked();
    return options;
}
