#include "datasetwizard.h"

#include "../core/imagefoldersource.h"
#include "recentpaths.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextEdit>
#include <QThread>
#include <QVBoxLayout>
#include <QWizardPage>

namespace {

const int kKindRole = Qt::UserRole + 1;
const int kPathRole = Qt::UserRole + 2;

QString videoFilter()
{
    return QStringLiteral("Videos (*.mp4 *.avi *.mov *.mkv *.wmv *.flv *.m4v *.mpg *.mpeg "
                          "*.webm);;All files (*)");
}

// Pages whose Next/Finish button depends on state the wizard owns rather than on
// a mandatory field. No Q_OBJECT: they override a virtual and add nothing that
// moc would need to see.
class GatedPage : public QWizardPage
{
public:
    explicit GatedPage(const bool *gate, QWidget *parent = nullptr)
        : QWizardPage(parent)
        , m_gate(gate)
    {
    }

    bool isComplete() const override { return m_gate && *m_gate; }

    // completeChanged() is protected against being emitted from elsewhere, so
    // the wizard nudges the page and the page re-reads the gate.
    void refreshComplete() { emit completeChanged(); }

private:
    const bool *m_gate;
};

} // namespace

DatasetWizard::DatasetWizard(QWidget *parent)
    : QWizard(parent)
    , m_sourceList(new QListWidget(this))
    , m_sourceSummary(new QLabel(this))
    , m_modeCombo(new QComboBox(this))
    , m_everyNthSpin(new QSpinBox(this))
    , m_targetFpsSpin(new QDoubleSpinBox(this))
    , m_maxFramesSpin(new QSpinBox(this))
    , m_blurCheck(new QCheckBox(QStringLiteral("Skip blurred frames"), this))
    , m_blurSpin(new QDoubleSpinBox(this))
    , m_similarCheck(new QCheckBox(QStringLiteral("Skip near-duplicate frames"), this))
    , m_similarSpin(new QDoubleSpinBox(this))
    , m_resizeCheck(new QCheckBox(QStringLiteral("Downscale to fit"), this))
    , m_resizeWidthSpin(new QSpinBox(this))
    , m_resizeHeightSpin(new QSpinBox(this))
    , m_formatCombo(new QComboBox(this))
    , m_qualitySpin(new QSpinBox(this))
    , m_estimateLabel(new QLabel(this))
    , m_outputEdit(new QLineEdit(this))
    , m_autoAnnotateCheck(
          new QCheckBox(QStringLiteral("Run the detection model once the dataset opens"), this))
    , m_progressBar(new QProgressBar(this))
    , m_progressLabel(new QLabel(this))
    , m_summary(new QTextEdit(this))
    , m_cancelBuildButton(new QPushButton(QStringLiteral("Stop"), this))
{
    setWindowTitle(QStringLiteral("Create Dataset"));
    setWizardStyle(QWizard::ModernStyle);
    setOption(QWizard::NoBackButtonOnLastPage, true);
    resize(720, 560);

    setPage(SourcesPage, createSourcesPage());
    setPage(SamplingPage, createSamplingPage());
    setPage(OutputPage, createOutputPage());
    setPage(RunPage, createRunPage());
    setStartId(SourcesPage);

    connect(this, &QWizard::currentIdChanged, this, &DatasetWizard::onPageChanged);
}

DatasetWizard::~DatasetWizard()
{
    if (m_thread) {
        if (m_builder)
            m_builder->cancel();
        m_thread->quit();
        if (!m_thread->wait(5000))
            m_thread->terminate();
    }
}

QWizardPage *DatasetWizard::createSourcesPage()
{
    auto *page = new GatedPage(&m_hasSources);
    page->setTitle(QStringLiteral("Sources"));
    page->setSubTitle(QStringLiteral("Add the videos and image folders this dataset is built "
                                     "from. They are combined into one flat set of frames."));

    auto *addVideos = new QPushButton(QStringLiteral("Add videos..."), page);
    auto *addFolder = new QPushButton(QStringLiteral("Add image folder..."), page);
    auto *remove = new QPushButton(QStringLiteral("Remove"), page);

    m_sourceSummary->setStyleSheet(QStringLiteral("color: gray;"));

    auto *buttons = new QVBoxLayout;
    buttons->addWidget(addVideos);
    buttons->addWidget(addFolder);
    buttons->addWidget(remove);
    buttons->addStretch();

    auto *row = new QHBoxLayout;
    row->addWidget(m_sourceList, 1);
    row->addLayout(buttons);

    auto *layout = new QVBoxLayout(page);
    layout->addLayout(row, 1);
    layout->addWidget(m_sourceSummary);

    connect(addVideos, &QPushButton::clicked, this, &DatasetWizard::onAddVideos);
    connect(addFolder, &QPushButton::clicked, this, &DatasetWizard::onAddFolder);
    connect(remove, &QPushButton::clicked, this, &DatasetWizard::onRemoveSource);

    refreshSourceList();
    return page;
}

QWizardPage *DatasetWizard::createSamplingPage()
{
    auto *page = new QWizardPage;
    page->setTitle(QStringLiteral("Sampling"));
    page->setSubTitle(QStringLiteral("Consecutive video frames are nearly identical. Sampling "
                                     "sparsely gives a dataset that is far cheaper to label "
                                     "and no less informative."));

    m_modeCombo->addItem(QStringLiteral("Every Nth frame"),
                         static_cast<int>(SamplingOptions::Mode::EveryNth));
    m_modeCombo->addItem(QStringLiteral("Target frame rate"),
                         static_cast<int>(SamplingOptions::Mode::TargetFps));
    m_modeCombo->addItem(QStringLiteral("Every frame"),
                         static_cast<int>(SamplingOptions::Mode::All));

    m_everyNthSpin->setRange(1, 10000);
    m_everyNthSpin->setValue(10);

    m_targetFpsSpin->setRange(0.1, 120.0);
    m_targetFpsSpin->setValue(2.0);
    m_targetFpsSpin->setSuffix(QStringLiteral(" fps"));
    m_targetFpsSpin->setToolTip(QStringLiteral("Applies to videos. Image folders have no frame "
                                               "rate and fall back to every frame."));

    m_maxFramesSpin->setRange(0, 1000000);
    m_maxFramesSpin->setValue(0);
    m_maxFramesSpin->setSpecialValueText(QStringLiteral("no limit"));
    m_maxFramesSpin->setToolTip(QStringLiteral("Caps how many frames any one source can "
                                               "contribute, so one long video does not "
                                               "dominate the dataset."));

    m_blurSpin->setRange(1.0, 2000.0);
    m_blurSpin->setValue(60.0);
    m_blurSpin->setDecimals(0);
    m_blurSpin->setEnabled(false);
    m_blurSpin->setToolTip(QStringLiteral("Laplacian variance. Lower keeps more; around 60 "
                                          "rejects obvious motion blur. Worth testing on a "
                                          "short clip before a long run."));

    m_similarSpin->setRange(0.001, 1.0);
    m_similarSpin->setValue(0.04);
    m_similarSpin->setDecimals(3);
    m_similarSpin->setSingleStep(0.005);
    m_similarSpin->setEnabled(false);
    m_similarSpin->setToolTip(QStringLiteral("Mean pixel difference from the last frame kept. "
                                             "Higher demands more change before a frame is "
                                             "accepted."));

    m_resizeWidthSpin->setRange(64, 16384);
    m_resizeWidthSpin->setValue(1920);
    m_resizeHeightSpin->setRange(64, 16384);
    m_resizeHeightSpin->setValue(1080);
    m_resizeWidthSpin->setEnabled(false);
    m_resizeHeightSpin->setEnabled(false);

    m_formatCombo->addItem(QStringLiteral("JPEG"), QStringLiteral("jpg"));
    m_formatCombo->addItem(QStringLiteral("PNG (lossless)"), QStringLiteral("png"));

    m_qualitySpin->setRange(1, 100);
    m_qualitySpin->setValue(92);

    m_estimateLabel->setStyleSheet(QStringLiteral("color: gray;"));
    m_estimateLabel->setWordWrap(true);

    auto *modeForm = new QFormLayout;
    modeForm->addRow(QStringLiteral("Take:"), m_modeCombo);
    modeForm->addRow(QStringLiteral("One in every:"), m_everyNthSpin);
    modeForm->addRow(QStringLiteral("Target rate:"), m_targetFpsSpin);
    modeForm->addRow(QStringLiteral("Max per source:"), m_maxFramesSpin);

    auto *blurRow = new QHBoxLayout;
    blurRow->addWidget(m_blurCheck);
    blurRow->addWidget(new QLabel(QStringLiteral("threshold:"), page));
    blurRow->addWidget(m_blurSpin);
    blurRow->addStretch();

    auto *similarRow = new QHBoxLayout;
    similarRow->addWidget(m_similarCheck);
    similarRow->addWidget(new QLabel(QStringLiteral("min change:"), page));
    similarRow->addWidget(m_similarSpin);
    similarRow->addStretch();

    auto *filters = new QGroupBox(QStringLiteral("Reject unusable frames"), page);
    auto *filterLayout = new QVBoxLayout(filters);
    filterLayout->addLayout(blurRow);
    filterLayout->addLayout(similarRow);

    auto *resizeRow = new QHBoxLayout;
    resizeRow->addWidget(m_resizeCheck);
    resizeRow->addWidget(m_resizeWidthSpin);
    resizeRow->addWidget(new QLabel(QStringLiteral("x"), page));
    resizeRow->addWidget(m_resizeHeightSpin);
    resizeRow->addStretch();

    auto *outputForm = new QFormLayout;
    outputForm->addRow(QStringLiteral("Format:"), m_formatCombo);
    outputForm->addRow(QStringLiteral("JPEG quality:"), m_qualitySpin);

    auto *encoding = new QGroupBox(QStringLiteral("Output images"), page);
    auto *encodingLayout = new QVBoxLayout(encoding);
    encodingLayout->addLayout(resizeRow);
    encodingLayout->addLayout(outputForm);

    auto *layout = new QVBoxLayout(page);
    layout->addLayout(modeForm);
    layout->addWidget(filters);
    layout->addWidget(encoding);
    layout->addWidget(m_estimateLabel);
    layout->addStretch();

    connect(m_modeCombo, &QComboBox::currentIndexChanged, this,
            &DatasetWizard::onSamplingChanged);
    connect(m_everyNthSpin, &QSpinBox::valueChanged, this, &DatasetWizard::onSamplingChanged);
    connect(m_targetFpsSpin, &QDoubleSpinBox::valueChanged, this,
            &DatasetWizard::onSamplingChanged);
    connect(m_maxFramesSpin, &QSpinBox::valueChanged, this, &DatasetWizard::onSamplingChanged);
    connect(m_blurCheck, &QCheckBox::toggled, m_blurSpin, &QWidget::setEnabled);
    connect(m_similarCheck, &QCheckBox::toggled, m_similarSpin, &QWidget::setEnabled);
    connect(m_resizeCheck, &QCheckBox::toggled, m_resizeWidthSpin, &QWidget::setEnabled);
    connect(m_resizeCheck, &QCheckBox::toggled, m_resizeHeightSpin, &QWidget::setEnabled);
    connect(m_formatCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_qualitySpin->setEnabled(m_formatCombo->itemData(index).toString()
                                  == QLatin1String("jpg"));
    });

    onSamplingChanged();
    return page;
}

QWizardPage *DatasetWizard::createOutputPage()
{
    auto *page = new GatedPage(&m_hasOutputDirectory);
    page->setTitle(QStringLiteral("Destination"));
    page->setSubTitle(QStringLiteral("Frames are written to an \"images\" folder inside this "
                                     "directory, which then opens as a project."));

    auto *browse = new QPushButton(QStringLiteral("Browse..."), page);

    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    m_outputEdit->setText(QDir(pictures).filePath(QStringLiteral("PicAnnotate Dataset")));

    m_autoAnnotateCheck->setChecked(false);
    m_autoAnnotateCheck->setToolTip(
        QStringLiteral("Runs the model selected in the Predictions panel over every extracted "
                       "frame. Results arrive as predictions for you to review, not as "
                       "annotations."));

    auto *row = new QHBoxLayout;
    row->addWidget(m_outputEdit);
    row->addWidget(browse);

    auto *form = new QFormLayout;
    form->addRow(QStringLiteral("Output folder:"), row);

    auto *note = new QLabel(
        QStringLiteral("Existing files with the same names will be overwritten."), page);
    note->setStyleSheet(QStringLiteral("color: gray;"));
    note->setWordWrap(true);

    auto *layout = new QVBoxLayout(page);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addSpacing(12);
    layout->addWidget(m_autoAnnotateCheck);
    layout->addStretch();

    connect(browse, &QPushButton::clicked, this, &DatasetWizard::onBrowseOutput);
    connect(m_outputEdit, &QLineEdit::textChanged, this, [this, page](const QString &text) {
        m_hasOutputDirectory = !text.trimmed().isEmpty();
        page->refreshComplete();
    });

    m_hasOutputDirectory = !m_outputEdit->text().trimmed().isEmpty();
    return page;
}

QWizardPage *DatasetWizard::createRunPage()
{
    auto *page = new GatedPage(&m_buildSucceeded);
    page->setTitle(QStringLiteral("Extracting"));
    page->setSubTitle(QStringLiteral("Decoding and writing frames."));

    m_progressBar->setRange(0, 0);
    m_progressLabel->setWordWrap(true);

    m_summary->setReadOnly(true);
    m_summary->setVisible(false);

    auto *cancelRow = new QHBoxLayout;
    cancelRow->addStretch();
    cancelRow->addWidget(m_cancelBuildButton);

    auto *layout = new QVBoxLayout(page);
    layout->addWidget(m_progressLabel);
    layout->addWidget(m_progressBar);
    layout->addLayout(cancelRow);
    layout->addWidget(m_summary, 1);

    connect(m_cancelBuildButton, &QPushButton::clicked, this, [this] {
        if (m_builder)
            m_builder->cancel();
        m_cancelBuildButton->setEnabled(false);
        m_progressLabel->setText(QStringLiteral("Stopping after the current frame..."));
    });

    // Finishing is only allowed once the run is over; isComplete() is driven by
    // m_finished through completeChanged().
    page->setFinalPage(true);
    return page;
}

void DatasetWizard::onAddVideos()
{
    const QStringList paths = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Add videos"), RecentPaths::dir(RecentPaths::Videos), videoFilter());
    if (!paths.isEmpty())
        RecentPaths::remember(RecentPaths::Videos, paths.last());
    for (const QString &path : paths)
        addSource({IngestSource::Kind::Video, path});
    refreshSourceList();
}

void DatasetWizard::onAddFolder()
{
    const QString path = QFileDialog::getExistingDirectory(this, QStringLiteral("Add image folder"),
                                                           RecentPaths::dir(RecentPaths::Images));
    if (path.isEmpty())
        return;
    RecentPaths::remember(RecentPaths::Images, path);
    addSource({IngestSource::Kind::ImageFolder, path});
    refreshSourceList();
}

void DatasetWizard::addSource(const IngestSource &source)
{
    for (const IngestSource &existing : m_sources) {
        if (existing.path == source.path && existing.kind == source.kind)
            return;   // adding the same source twice would duplicate its frames
    }
    m_sources.append(source);
}

void DatasetWizard::onRemoveSource()
{
    const int row = m_sourceList->currentRow();
    if (row >= 0 && row < m_sources.size()) {
        m_sources.remove(row);
        refreshSourceList();
    }
}

void DatasetWizard::refreshSourceList()
{
    m_sourceList->clear();
    for (const IngestSource &source : m_sources) {
        auto *item = new QListWidgetItem(
            QStringLiteral("%1  —  %2")
                .arg(source.kind == IngestSource::Kind::Video ? QStringLiteral("Video")
                                                              : QStringLiteral("Folder"),
                     source.path),
            m_sourceList);
        item->setData(kKindRole, static_cast<int>(source.kind));
        item->setData(kPathRole, source.path);
    }

    m_sourceSummary->setText(m_sources.isEmpty()
                                 ? QStringLiteral("No sources yet.")
                                 : QStringLiteral("%1 source(s).").arg(m_sources.size()));

    m_hasSources = !m_sources.isEmpty();
    // static_cast, not qobject_cast: GatedPage has no Q_OBJECT (it adds no
    // signals of its own), and these ids were populated with GatedPages here.
    if (auto *page = static_cast<GatedPage *>(this->page(SourcesPage)))
        page->refreshComplete();
}

void DatasetWizard::onBrowseOutput()
{
    // The suggested output folder usually does not exist yet, and QFileDialog
    // ignores a path it cannot open; fall back to the last folder used instead
    // of letting it drop back to the working directory.
    const QString current = m_outputEdit->text();
    const QString start = QFileInfo(current).isDir()
                              ? current
                              : RecentPaths::dir(RecentPaths::Exports,
                                                 QFileInfo(current).absolutePath());

    const QString path =
        QFileDialog::getExistingDirectory(this, QStringLiteral("Choose an output folder"), start);
    if (!path.isEmpty()) {
        m_outputEdit->setText(path);
        RecentPaths::remember(RecentPaths::Exports, path);
    }
}

void DatasetWizard::onSamplingChanged()
{
    const auto mode = static_cast<SamplingOptions::Mode>(m_modeCombo->currentData().toInt());
    m_everyNthSpin->setEnabled(mode == SamplingOptions::Mode::EveryNth);
    m_targetFpsSpin->setEnabled(mode == SamplingOptions::Mode::TargetFps);
    refreshEstimate();
}

void DatasetWizard::refreshEstimate()
{
    if (m_sources.isEmpty()) {
        m_estimateLabel->setText(QString());
        return;
    }

    const SamplingOptions sampling = collectSampling();
    int total = 0;
    for (const IngestSource &source : m_sources)
        total += DatasetBuilder::estimateFrameCount(source, sampling);

    QString text = QStringLiteral("About %1 image(s) will be written.").arg(total);
    if (m_blurCheck->isChecked() || m_similarCheck->isChecked()) {
        text += QStringLiteral(" The blur and duplicate filters can only reduce this, by an "
                               "amount that depends on the footage.");
    }
    m_estimateLabel->setText(text);
}

SamplingOptions DatasetWizard::collectSampling() const
{
    SamplingOptions sampling;
    sampling.mode = static_cast<SamplingOptions::Mode>(m_modeCombo->currentData().toInt());
    sampling.everyNth = m_everyNthSpin->value();
    sampling.targetFps = m_targetFpsSpin->value();
    sampling.maxFramesPerSource = m_maxFramesSpin->value();
    sampling.skipBlurred = m_blurCheck->isChecked();
    sampling.blurThreshold = m_blurSpin->value();
    sampling.skipSimilar = m_similarCheck->isChecked();
    sampling.differenceThreshold = m_similarSpin->value();
    if (m_resizeCheck->isChecked())
        sampling.resizeTo = QSize(m_resizeWidthSpin->value(), m_resizeHeightSpin->value());
    sampling.imageFormat = m_formatCombo->currentData().toString();
    sampling.jpegQuality = m_qualitySpin->value();
    return sampling;
}

BuildOptions DatasetWizard::collectOptions() const
{
    BuildOptions options;
    options.outputDirectory = m_outputEdit->text().trimmed();
    options.sources = m_sources;
    options.sampling = collectSampling();
    return options;
}

bool DatasetWizard::shouldAutoAnnotate() const
{
    return m_autoAnnotateCheck->isChecked();
}

void DatasetWizard::onPageChanged(int id)
{
    if (id == SamplingPage) {
        refreshEstimate();
        return;
    }
    if (id == RunPage && !m_running && !m_finished)
        startBuild();
}

void DatasetWizard::startBuild()
{
    m_running = true;
    m_finished = false;
    m_summary->setVisible(false);
    m_cancelBuildButton->setEnabled(true);
    m_progressBar->setRange(0, 0);
    m_progressLabel->setText(QStringLiteral("Starting..."));

    // Back and Finish stay disabled until the run ends, so the wizard cannot be
    // dismissed out from under a thread that is still writing files.
    button(QWizard::BackButton)->setEnabled(false);
    button(QWizard::FinishButton)->setEnabled(false);

    m_thread = new QThread(this);
    m_builder = new DatasetBuilder;
    m_builder->moveToThread(m_thread);

    connect(m_thread, &QThread::finished, m_builder, &QObject::deleteLater);
    connect(m_builder, &DatasetBuilder::progress, this, &DatasetWizard::onBuildProgress);
    connect(m_builder, &DatasetBuilder::finished, this, &DatasetWizard::onBuildFinished);

    m_thread->start();
    QMetaObject::invokeMethod(m_builder, "run", Qt::QueuedConnection,
                              Q_ARG(BuildOptions, collectOptions()));
}

void DatasetWizard::onBuildProgress(int sourceIndex, int sourceCount, const QString &sourceName,
                                    int framesWritten, int framesExamined)
{
    m_progressLabel->setText(QStringLiteral("Source %1 of %2 — %3\n"
                                            "%4 frame(s) written, %5 examined.")
                                 .arg(sourceIndex + 1)
                                 .arg(sourceCount)
                                 .arg(sourceName)
                                 .arg(framesWritten)
                                 .arg(framesExamined));
}

void DatasetWizard::onBuildFinished(const BuildReport &report)
{
    m_report = report;
    m_running = false;
    m_finished = true;
    m_buildSucceeded = report.ok;
    if (auto *runPage = static_cast<GatedPage *>(this->page(RunPage)))
        runPage->refreshComplete();

    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(1);
    m_cancelBuildButton->setEnabled(false);

    m_progressLabel->setText(report.ok ? QStringLiteral("Done.")
                                       : QStringLiteral("Finished with problems."));
    m_summary->setPlainText(report.toText());
    m_summary->setVisible(true);

    if (QWizardPage *page = this->page(RunPage))
        page->setSubTitle(report.ok ? QStringLiteral("The dataset is ready.")
                                    : QStringLiteral("Nothing was written."));

    // Only offer Finish when there is a dataset to hand back; otherwise Back is
    // the useful button, so the settings can be corrected.
    button(QWizard::FinishButton)->setEnabled(report.ok);
    button(QWizard::BackButton)->setEnabled(!report.ok);

    if (m_thread) {
        m_thread->quit();
        m_thread->wait(5000);
        m_thread->deleteLater();
        m_thread = nullptr;
        m_builder = nullptr;
    }
}

void DatasetWizard::done(int result)
{
    // Closing mid-run would leave a thread writing into a folder nobody is
    // watching; stop it and wait rather than detaching it.
    if (m_running && m_builder) {
        const auto reply = QMessageBox::question(
            this, QStringLiteral("Create Dataset"),
            QStringLiteral("Extraction is still running. Stop it and close?"),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return;

        m_builder->cancel();
        if (m_thread) {
            m_thread->quit();
            m_thread->wait(5000);
        }
    }

    QWizard::done(result);
}
