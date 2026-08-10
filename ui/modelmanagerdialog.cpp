#include "modelmanagerdialog.h"

#include "../ai/modeldownload.h"
#include "../ai/modelmanager.h"
#include "recentpaths.h"

#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const int kModelIdRole = Qt::UserRole + 1;

// Tasks in the order the dialog groups them, which is roughly the order a user
// meets them: detect something, refine it, then name it.
const ModelTask kGroupedTasks[] = {
    ModelTask::Detection,
    ModelTask::Segmentation,
    ModelTask::SamEncoder,
    ModelTask::SamDecoder,
    ModelTask::ClipImage,
    ModelTask::ClipText,
    ModelTask::Classification,
};

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0)
        return QString();
    if (bytes < 1024LL * 1024LL)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 0);
    return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
}

// Small form for registering a model the catalogue does not ship. No Q_OBJECT:
// it has no signals of its own and everything is wired with lambdas.
class AddModelDialog : public QDialog
{
public:
    explicit AddModelDialog(QWidget *parent)
        : QDialog(parent)
        , m_id(new QLineEdit(this))
        , m_name(new QLineEdit(this))
        , m_task(new QComboBox(this))
        , m_runtime(new QComboBox(this))
        , m_file(new QLineEdit(this))
        , m_url(new QLineEdit(this))
        , m_sha(new QLineEdit(this))
    {
        setWindowTitle(QStringLiteral("Add Model"));

        for (ModelTask task : kGroupedTasks)
            m_task->addItem(modelTaskDisplayName(task), static_cast<int>(task));

        m_runtime->addItem(modelRuntimeDisplayName(ModelRuntime::OnnxRuntime),
                           static_cast<int>(ModelRuntime::OnnxRuntime));
        m_runtime->addItem(modelRuntimeDisplayName(ModelRuntime::OpenCvDnn),
                           static_cast<int>(ModelRuntime::OpenCvDnn));

        m_id->setPlaceholderText(QStringLiteral("my-yolov8-custom"));
        m_name->setPlaceholderText(QStringLiteral("My fine-tuned detector"));
        m_url->setPlaceholderText(QStringLiteral("optional https:// download URL"));
        m_sha->setPlaceholderText(QStringLiteral("optional SHA-256, verified after download"));

        auto *browse = new QPushButton(QStringLiteral("Browse..."), this);
        auto *fileRow = new QHBoxLayout;
        fileRow->addWidget(m_file);
        fileRow->addWidget(browse);

        auto *form = new QFormLayout;
        form->addRow(QStringLiteral("Id:"), m_id);
        form->addRow(QStringLiteral("Name:"), m_name);
        form->addRow(QStringLiteral("Task:"), m_task);
        form->addRow(QStringLiteral("Runtime:"), m_runtime);
        form->addRow(QStringLiteral("ONNX file:"), fileRow);
        form->addRow(QStringLiteral("Download URL:"), m_url);
        form->addRow(QStringLiteral("SHA-256:"), m_sha);

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

        auto *layout = new QVBoxLayout(this);
        auto *note = new QLabel(
            QStringLiteral("Pick the file you already have, or give a URL to download it from. "
                           "An id that matches a built-in model replaces that entry."),
            this);
        note->setWordWrap(true);
        note->setStyleSheet(QStringLiteral("color: gray;"));
        layout->addWidget(note);
        layout->addLayout(form);
        layout->addWidget(buttons);

        connect(browse, &QPushButton::clicked, this, [this] {
            const QString start = m_file->text().isEmpty()
                                      ? RecentPaths::dir(RecentPaths::Models,
                                                         ModelManager::downloadDirectory())
                                      : m_file->text();
            const QString path = QFileDialog::getOpenFileName(
                this, QStringLiteral("Select model file"), start,
                QStringLiteral("ONNX models (*.onnx);;All files (*)"));
            if (!path.isEmpty()) {
                RecentPaths::remember(RecentPaths::Models, path);
                m_file->setText(path);
                if (m_id->text().isEmpty())
                    m_id->setText(QFileInfo(path).completeBaseName());
            }
        });
        connect(buttons, &QDialogButtonBox::accepted, this, [this] {
            if (m_id->text().trimmed().isEmpty()) {
                QMessageBox::warning(this, QStringLiteral("Add Model"),
                                     QStringLiteral("An id is required."));
                return;
            }
            if (m_file->text().trimmed().isEmpty() && m_url->text().trimmed().isEmpty()) {
                QMessageBox::warning(
                    this, QStringLiteral("Add Model"),
                    QStringLiteral("Give either a file on disk or a download URL."));
                return;
            }
            accept();
        });
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    }

    ModelSpec spec() const
    {
        ModelSpec result;
        result.id = m_id->text().trimmed();
        result.displayName = m_name->text().trimmed().isEmpty() ? result.id
                                                                : m_name->text().trimmed();
        result.description = QStringLiteral("Added by you.");
        result.task = static_cast<ModelTask>(m_task->currentData().toInt());
        result.runtime = static_cast<ModelRuntime>(m_runtime->currentData().toInt());
        result.downloadUrl = m_url->text().trimmed();
        result.sha256 = m_sha->text().trimmed().toLower();

        const QString chosen = m_file->text().trimmed();
        result.fileName = chosen.isEmpty() ? result.id + QStringLiteral(".onnx")
                                           : QFileInfo(chosen).fileName();
        return result;
    }

    // Absolute path the user picked, empty when they only gave a URL.
    QString chosenFilePath() const { return m_file->text().trimmed(); }

private:
    QLineEdit *m_id;
    QLineEdit *m_name;
    QComboBox *m_task;
    QComboBox *m_runtime;
    QLineEdit *m_file;
    QLineEdit *m_url;
    QLineEdit *m_sha;
};

} // namespace

ModelManagerDialog::ModelManagerDialog(QWidget *parent)
    : QDialog(parent)
    , m_runtimeBanner(new QLabel(this))
    , m_providerCombo(new QComboBox(this))
    , m_tree(new QTreeWidget(this))
    , m_detailTitle(new QLabel(this))
    , m_detailBody(new QLabel(this))
    , m_detailPath(new QLabel(this))
    , m_detailHint(new QLabel(this))
    , m_progress(new QProgressBar(this))
    , m_useButton(new QPushButton(QStringLiteral("Use for this task"), this))
    , m_locateButton(new QPushButton(QStringLiteral("Locate file..."), this))
    , m_downloadButton(new QPushButton(QStringLiteral("Download"), this))
    , m_clearButton(new QPushButton(QStringLiteral("Clear override"), this))
    , m_addButton(new QPushButton(QStringLiteral("Add model..."), this))
    , m_removeButton(new QPushButton(QStringLiteral("Remove"), this))
    , m_buttons(new QDialogButtonBox(QDialogButtonBox::Close, this))
{
    setWindowTitle(QStringLiteral("Model Manager"));
    resize(940, 600);

    m_runtimeBanner->setWordWrap(true);

    m_providerCombo->addItem(executionProviderDisplayName(ExecutionProvider::Cpu),
                             static_cast<int>(ExecutionProvider::Cpu));
    m_providerCombo->addItem(executionProviderDisplayName(ExecutionProvider::DirectMl),
                             static_cast<int>(ExecutionProvider::DirectMl));
    m_providerCombo->addItem(executionProviderDisplayName(ExecutionProvider::Cuda),
                             static_cast<int>(ExecutionProvider::Cuda));
    m_providerCombo->setToolTip(
        QStringLiteral("Requested execution provider. If the ONNX Runtime build does not have "
                       "it, models fall back to CPU and say so when they load."));

    m_tree->setColumnCount(4);
    m_tree->setHeaderLabels({QStringLiteral("Model"), QStringLiteral("Runtime"),
                             QStringLiteral("Status"), QStringLiteral("In use")});
    m_tree->setRootIsDecorated(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);

    m_detailTitle->setStyleSheet(QStringLiteral("font-weight: bold;"));
    for (QLabel *label : {m_detailBody, m_detailPath, m_detailHint}) {
        label->setWordWrap(true);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    m_detailPath->setStyleSheet(QStringLiteral("color: gray;"));
    m_detailHint->setStyleSheet(QStringLiteral("font-family: monospace;"));
    m_detailHint->setOpenExternalLinks(true);

    m_progress->setVisible(false);
    m_progress->setRange(0, 100);

    auto *actionColumn = new QVBoxLayout;
    actionColumn->addWidget(m_useButton);
    actionColumn->addWidget(m_downloadButton);
    actionColumn->addWidget(m_locateButton);
    actionColumn->addWidget(m_clearButton);
    actionColumn->addSpacing(12);
    actionColumn->addWidget(m_addButton);
    actionColumn->addWidget(m_removeButton);
    actionColumn->addStretch();

    auto *details = new QGroupBox(QStringLiteral("Details"), this);
    auto *detailLayout = new QVBoxLayout(details);
    detailLayout->addWidget(m_detailTitle);
    detailLayout->addWidget(m_detailBody);
    detailLayout->addWidget(m_detailPath);
    detailLayout->addWidget(m_detailHint);
    detailLayout->addWidget(m_progress);
    detailLayout->addStretch();

    auto *middle = new QHBoxLayout;
    middle->addWidget(m_tree, 3);
    middle->addLayout(actionColumn);

    auto *providerRow = new QHBoxLayout;
    providerRow->addWidget(new QLabel(QStringLiteral("Run models on:"), this));
    providerRow->addWidget(m_providerCombo);
    providerRow->addStretch();

    auto *openFolder = new QPushButton(QStringLiteral("Open models folder"), this);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_runtimeBanner);
    layout->addLayout(providerRow);
    layout->addLayout(middle, 3);
    layout->addWidget(details, 2);

    m_buttons->addButton(openFolder, QDialogButtonBox::ActionRole);
    layout->addWidget(m_buttons);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            &ModelManagerDialog::onSelectionChanged);
    connect(m_useButton, &QPushButton::clicked, this, &ModelManagerDialog::onUseClicked);
    connect(m_locateButton, &QPushButton::clicked, this, &ModelManagerDialog::onLocateClicked);
    connect(m_downloadButton, &QPushButton::clicked, this, &ModelManagerDialog::onDownloadClicked);
    connect(m_clearButton, &QPushButton::clicked, this,
            &ModelManagerDialog::onClearOverrideClicked);
    connect(m_addButton, &QPushButton::clicked, this, &ModelManagerDialog::onAddCustomClicked);
    connect(m_removeButton, &QPushButton::clicked, this,
            &ModelManagerDialog::onRemoveCustomClicked);
    connect(openFolder, &QPushButton::clicked, this, &ModelManagerDialog::onOpenFolderClicked);
    connect(m_providerCombo, &QComboBox::currentIndexChanged, this,
            &ModelManagerDialog::onProviderChanged);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(&ModelManager::instance(), &ModelManager::availabilityChanged, this,
            &ModelManagerDialog::onAvailabilityChanged);

    const ExecutionProvider provider = ModelManager::instance().preferredProvider();
    m_providerCombo->setCurrentIndex(m_providerCombo->findData(static_cast<int>(provider)));

    refreshRuntimeBanner();
    buildTree();
    refreshDetails();
}

void ModelManagerDialog::refreshRuntimeBanner()
{
    if (ModelManager::onnxRuntimeAvailable()) {
        m_runtimeBanner->setText(
            QStringLiteral("ONNX Runtime %1 is available. OpenCV DNN handles the YOLO models.")
                .arg(ModelManager::onnxRuntimeVersion()));
        m_runtimeBanner->setStyleSheet(QStringLiteral("color: gray;"));
    } else {
        m_runtimeBanner->setText(
            QStringLiteral("This build has no ONNX Runtime, so SAM and CLIP cannot run. "
                           "YOLO still works through OpenCV DNN. See docs/AI_SETUP.md to "
                           "rebuild with ONNX Runtime."));
        m_runtimeBanner->setStyleSheet(QStringLiteral("color: #c07000; font-weight: bold;"));
    }
}

void ModelManagerDialog::buildTree()
{
    const QString previous = currentModelId();

    m_tree->clear();
    const ModelManager &manager = ModelManager::instance();

    for (ModelTask task : kGroupedTasks) {
        const QVector<ModelSpec> specs = manager.registry().modelsForTask(task);
        if (specs.isEmpty())
            continue;

        auto *group = new QTreeWidgetItem(m_tree);
        group->setText(0, modelTaskDisplayName(task));
        group->setFirstColumnSpanned(true);
        group->setFlags(group->flags() & ~Qt::ItemIsSelectable);
        QFont bold = group->font(0);
        bold.setBold(true);
        group->setFont(0, bold);

        for (const ModelSpec &spec : specs) {
            auto *item = new QTreeWidgetItem(group);
            item->setData(0, kModelIdRole, spec.id);
            refreshItem(item);
            if (spec.id == previous)
                m_tree->setCurrentItem(item);
        }

        group->setExpanded(true);
    }
}

void ModelManagerDialog::refreshItem(QTreeWidgetItem *item)
{
    if (!item)
        return;

    const QString id = item->data(0, kModelIdRole).toString();
    const ModelManager &manager = ModelManager::instance();
    const ModelSpec *spec = manager.registry().model(id);
    if (!spec)
        return;

    item->setText(0, spec->displayName);
    item->setText(1, modelRuntimeDisplayName(spec->runtime));

    QString reason;
    const bool usable = manager.isUsable(id, &reason);
    const bool onDisk = manager.isAvailable(id);

    if (usable) {
        item->setText(2, QStringLiteral("Ready"));
        item->setForeground(2, QBrush());
    } else if (onDisk) {
        // The file is there but the runtime is not: worth distinguishing, since
        // downloading again would not help.
        item->setText(2, QStringLiteral("Runtime missing"));
        item->setForeground(2, QBrush(QColor(0xc0, 0x70, 0x00)));
    } else {
        item->setText(2, spec->isDownloadable() ? QStringLiteral("Not downloaded")
                                                : QStringLiteral("Not installed"));
        item->setForeground(2, QBrush(QColor(0x90, 0x90, 0x90)));
    }

    const bool selected = manager.selectedModelId(spec->task) == spec->id;
    item->setText(3, selected ? QStringLiteral("✓") : QString());
    item->setToolTip(3, selected
                            ? QStringLiteral("Used for %1").arg(modelTaskDisplayName(spec->task))
                            : QString());
    item->setToolTip(2, reason);
}

QString ModelManagerDialog::currentModelId() const
{
    const QTreeWidgetItem *item = m_tree->currentItem();
    if (!item)
        return QString();
    return item->data(0, kModelIdRole).toString();
}

const ModelSpec *ModelManagerDialog::currentSpec() const
{
    const QString id = currentModelId();
    return id.isEmpty() ? nullptr : ModelManager::instance().registry().model(id);
}

void ModelManagerDialog::onSelectionChanged()
{
    refreshDetails();
}

void ModelManagerDialog::refreshDetails()
{
    const ModelSpec *spec = currentSpec();
    ModelManager &manager = ModelManager::instance();

    if (!spec) {
        m_detailTitle->setText(QStringLiteral("No model selected"));
        m_detailBody->clear();
        m_detailPath->clear();
        m_detailHint->clear();
        m_useButton->setEnabled(false);
        m_locateButton->setEnabled(false);
        m_downloadButton->setEnabled(false);
        m_clearButton->setEnabled(false);
        m_removeButton->setEnabled(false);
        return;
    }

    const QString id = spec->id;
    const QString path = manager.localPath(id);
    const bool pinned = !manager.userPath(id).isEmpty();

    m_detailTitle->setText(spec->displayName);

    QString body = spec->description;
    if (!spec->license.isEmpty()) {
        body += QStringLiteral("\n\nLicence: %1").arg(spec->license);
        if (!spec->licenseUrl.isEmpty())
            body += QStringLiteral(" — %1").arg(spec->licenseUrl);
    }
    if (!spec->companionId.isEmpty()) {
        const ModelSpec *companion = manager.registry().model(spec->companionId);
        body += QStringLiteral("\n\nMust be paired with: %1")
                    .arg(companion ? companion->displayName : spec->companionId);
    }
    m_detailBody->setText(body);

    if (path.isEmpty()) {
        m_detailPath->setText(
            QStringLiteral("Not found. Looked for \"%1\" in:\n  %2")
                .arg(spec->fileName, manager.searchPaths().join(QStringLiteral("\n  "))));
    } else {
        m_detailPath->setText(pinned ? QStringLiteral("Pinned to: %1").arg(path)
                                     : QStringLiteral("Found at: %1").arg(path));
    }

    if (path.isEmpty() && !spec->sourceHint.isEmpty())
        m_detailHint->setText(spec->sourceHint);
    else
        m_detailHint->clear();

    const bool downloading = manager.isDownloading(id);
    m_useButton->setEnabled(manager.isUsable(id)
                            && manager.selectedModelId(spec->task) != id);
    m_locateButton->setEnabled(!downloading);
    m_downloadButton->setEnabled(spec->isDownloadable() && !downloading);
    m_downloadButton->setText(downloading ? QStringLiteral("Downloading...")
                                          : QStringLiteral("Download"));
    m_clearButton->setEnabled(pinned && !downloading);
    m_removeButton->setEnabled(manager.registry().isUserModel(id) && !downloading);

    if (!downloading && m_downloadingId != id)
        m_progress->setVisible(false);
}

void ModelManagerDialog::onUseClicked()
{
    const ModelSpec *spec = currentSpec();
    if (!spec)
        return;

    ModelManager::instance().setSelectedModelId(spec->task, spec->id);

    // The tick moves between siblings, so the whole group needs repainting.
    if (QTreeWidgetItem *item = m_tree->currentItem()) {
        if (QTreeWidgetItem *group = item->parent()) {
            for (int i = 0; i < group->childCount(); ++i)
                refreshItem(group->child(i));
        }
    }
    refreshDetails();
}

void ModelManagerDialog::onLocateClicked()
{
    const ModelSpec *spec = currentSpec();
    if (!spec)
        return;

    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Locate %1").arg(spec->displayName),
        RecentPaths::dir(RecentPaths::Models, ModelManager::downloadDirectory()),
        QStringLiteral("ONNX models (*.onnx);;All files (*)"));
    if (path.isEmpty())
        return;
    RecentPaths::remember(RecentPaths::Models, path);

    ModelManager::instance().setUserPath(spec->id, path);
}

void ModelManagerDialog::onClearOverrideClicked()
{
    if (const ModelSpec *spec = currentSpec())
        ModelManager::instance().clearUserPath(spec->id);
}

void ModelManagerDialog::onDownloadClicked()
{
    const ModelSpec *spec = currentSpec();
    if (!spec)
        return;

    // Downloading executes nothing, but it does put a multi-hundred-megabyte
    // file on disk from a third party, so the source and the licence are stated
    // before anything is fetched.
    QString message = QStringLiteral("Download %1 from:\n\n%2\n")
                          .arg(spec->displayName, spec->downloadUrl);
    if (spec->downloadBytes > 0)
        message += QStringLiteral("\nSize: about %1").arg(formatBytes(spec->downloadBytes));
    if (!spec->license.isEmpty())
        message += QStringLiteral("\nLicence: %1").arg(spec->license);
    message += spec->sha256.isEmpty()
                   ? QStringLiteral("\n\nNo checksum is recorded for this entry, so the "
                                    "download cannot be verified.")
                   : QStringLiteral("\n\nThe download will be verified against its "
                                    "recorded SHA-256.");
    message += QStringLiteral("\n\nIt will be saved to:\n%1")
                   .arg(ModelManager::downloadDirectory());

    if (QMessageBox::question(this, QStringLiteral("Download Model"), message,
                              QMessageBox::Ok | QMessageBox::Cancel)
        != QMessageBox::Ok) {
        return;
    }

    QString error;
    ModelDownload *download = ModelManager::instance().startDownload(spec->id, &error);
    if (!download) {
        QMessageBox::warning(this, QStringLiteral("Download Model"), error);
        return;
    }

    m_downloadingId = spec->id;
    m_progress->setVisible(true);
    m_progress->setRange(0, 0);   // indeterminate until the first size is known
    m_progress->setFormat(QStringLiteral("%1 — starting...").arg(spec->fileName));

    const QString modelId = spec->id;
    connect(download, &ModelDownload::progress, this,
            [this, modelId](qint64 received, qint64 total) {
                if (m_downloadingId != modelId)
                    return;
                if (total > 0) {
                    m_progress->setRange(0, 100);
                    m_progress->setValue(static_cast<int>(received * 100 / total));
                    m_progress->setFormat(QStringLiteral("%1 of %2 (%p%)")
                                              .arg(formatBytes(received), formatBytes(total)));
                } else {
                    m_progress->setFormat(QStringLiteral("%1 downloaded")
                                              .arg(formatBytes(received)));
                }
            });
    connect(download, &ModelDownload::finished, this,
            [this, modelId](bool ok, const QString &failure) {
                if (m_downloadingId == modelId) {
                    m_downloadingId.clear();
                    m_progress->setVisible(false);
                }
                if (!ok) {
                    QMessageBox::warning(this, QStringLiteral("Download Model"), failure);
                } else {
                    onAvailabilityChanged(modelId);
                }
                refreshDetails();
            });

    refreshDetails();
}

void ModelManagerDialog::onAvailabilityChanged(const QString &modelId)
{
    // Walk the tree rather than rebuilding it, so the selection and the scroll
    // position survive a download finishing.
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g) {
        QTreeWidgetItem *group = m_tree->topLevelItem(g);
        for (int i = 0; i < group->childCount(); ++i) {
            QTreeWidgetItem *item = group->child(i);
            if (item->data(0, kModelIdRole).toString() == modelId)
                refreshItem(item);
        }
    }
    refreshDetails();
}

void ModelManagerDialog::onOpenFolderClicked()
{
    const QString dir = ModelManager::downloadDirectory();
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void ModelManagerDialog::onAddCustomClicked()
{
    AddModelDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    const ModelSpec spec = dialog.spec();
    QString error;
    if (!ModelManager::instance().registry().addUserModel(spec, &error)) {
        QMessageBox::warning(this, QStringLiteral("Add Model"), error);
        return;
    }

    // A file chosen by hand lives wherever the user keeps it, so pin it rather
    // than expecting a copy in the models folder.
    const QString chosen = dialog.chosenFilePath();
    if (!chosen.isEmpty())
        ModelManager::instance().setUserPath(spec.id, chosen);

    buildTree();
    refreshDetails();
}

void ModelManagerDialog::onRemoveCustomClicked()
{
    const ModelSpec *spec = currentSpec();
    if (!spec)
        return;

    const QString id = spec->id;
    const QString name = spec->displayName;
    if (QMessageBox::question(this, QStringLiteral("Remove Model"),
                              QStringLiteral("Remove \"%1\" from the catalogue?\n\n"
                                             "The model file itself is not deleted.")
                                  .arg(name))
        != QMessageBox::Yes) {
        return;
    }

    QString error;
    if (!ModelManager::instance().registry().removeUserModel(id, &error)) {
        QMessageBox::warning(this, QStringLiteral("Remove Model"), error);
        return;
    }
    ModelManager::instance().clearUserPath(id);

    buildTree();
    refreshDetails();
}

void ModelManagerDialog::onProviderChanged(int index)
{
    if (index < 0)
        return;
    const auto provider = static_cast<ExecutionProvider>(m_providerCombo->itemData(index).toInt());
    ModelManager::instance().setPreferredProvider(provider);
}
