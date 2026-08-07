#ifndef EXPORTDATASETDIALOG_H
#define EXPORTDATASETDIALOG_H

#include <QDialog>

#include "../io/datasetformat.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;

// Format-driven export dialog: the task list, and whether "copy images" can be
// turned off, follow whatever the selected format and source actually support.
class ExportDatasetDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ExportDatasetDialog(bool videoSource, QWidget *parent = nullptr);

    const IDatasetFormat *selectedFormat() const;
    ExportOptions options() const;

private slots:
    void onFormatChanged();
    void onBrowseClicked();
    void validate();

private:
    bool m_videoSource;

    QComboBox *m_formatCombo;
    QLabel *m_formatDescription;
    QComboBox *m_taskCombo;
    QLineEdit *m_outputEdit;
    QPushButton *m_browseButton;
    QCheckBox *m_copyImagesCheck;
    QCheckBox *m_includeEmptyCheck;
    QDoubleSpinBox *m_valSplitSpin;
    QPushButton *m_exportButton;
};

#endif // EXPORTDATASETDIALOG_H
