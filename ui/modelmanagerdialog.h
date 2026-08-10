#ifndef MODELMANAGERDIALOG_H
#define MODELMANAGERDIALOG_H

#include "../ai/modelspec.h"

#include <QDialog>
#include <QString>

class QDialogButtonBox;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// Where models are inspected, located, downloaded and chosen.
//
// The list shows every model the catalogue knows about whether or not its file
// is present, because "this model exists but you do not have it yet" is the
// state a user is most often in, and a dialog that hides it just looks empty.
class ModelManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ModelManagerDialog(QWidget *parent = nullptr);

private slots:
    void onSelectionChanged();
    void onUseClicked();
    void onLocateClicked();
    void onDownloadClicked();
    void onClearOverrideClicked();
    void onOpenFolderClicked();
    void onAddCustomClicked();
    void onRemoveCustomClicked();
    void onProviderChanged(int index);
    void onAvailabilityChanged(const QString &modelId);

private:
    void buildTree();
    void refreshItem(QTreeWidgetItem *item);
    void refreshDetails();
    void refreshRuntimeBanner();

    // Model id of the selected row, empty when a task group header is selected.
    QString currentModelId() const;
    const ModelSpec *currentSpec() const;

    QLabel *m_runtimeBanner;
    QComboBox *m_providerCombo;
    QTreeWidget *m_tree;

    QLabel *m_detailTitle;
    QLabel *m_detailBody;
    QLabel *m_detailPath;
    QLabel *m_detailHint;
    QProgressBar *m_progress;

    QPushButton *m_useButton;
    QPushButton *m_locateButton;
    QPushButton *m_downloadButton;
    QPushButton *m_clearButton;
    QPushButton *m_addButton;
    QPushButton *m_removeButton;
    QDialogButtonBox *m_buttons;

    // The download currently attached to the progress bar, so Cancel knows what
    // to stop and a second row's selection does not orphan the bar.
    QString m_downloadingId;
};

#endif // MODELMANAGERDIALOG_H
