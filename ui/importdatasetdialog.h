#ifndef IMPORTDATASETDIALOG_H
#define IMPORTDATASETDIALOG_H

#include <QDialog>

#include "../io/datasetformat.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

class ImportDatasetDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ImportDatasetDialog(QWidget *parent = nullptr);

    const IDatasetFormat *selectedFormat() const;
    ImportOptions options() const;

private slots:
    void onFormatChanged();
    void onBrowseClicked();
    void validate();

private:
    QComboBox *m_formatCombo;
    QLabel *m_formatDescription;
    QLabel *m_pathLabel;
    QLineEdit *m_pathEdit;
    QPushButton *m_browseButton;
    QCheckBox *m_replaceCheck;
    QCheckBox *m_groupTracksCheck;
    QPushButton *m_importButton;
};

#endif // IMPORTDATASETDIALOG_H
