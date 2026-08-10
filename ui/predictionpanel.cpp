#include "predictionpanel.h"

#include "../ai/predictionstore.h"
#include "../core/labelschema.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace {

const int kPredictionIdRole = Qt::UserRole + 1;

// A filled swatch in the class colour, so a prediction reads the same way an
// annotation does in the label panel.
QIcon colorSwatch(const QColor &color)
{
    QPixmap pixmap(12, 12);
    pixmap.fill(color.isValid() ? color : QColor(0x90, 0x90, 0x90));
    return QIcon(pixmap);
}

} // namespace

PredictionPanel::PredictionPanel(QWidget *parent)
    : QWidget(parent)
    , m_taskCombo(new QComboBox(this))
    , m_runFrameButton(new QPushButton(QStringLiteral("Run on frame"), this))
    , m_runAllButton(new QPushButton(QStringLiteral("Run on all frames"), this))
    , m_cancelButton(new QPushButton(QStringLiteral("Cancel"), this))
    , m_thresholdSlider(new QSlider(Qt::Horizontal, this))
    , m_thresholdLabel(new QLabel(this))
    , m_list(new QListWidget(this))
    , m_summary(new QLabel(this))
    , m_acceptButton(new QPushButton(QStringLiteral("Accept"), this))
    , m_rejectButton(new QPushButton(QStringLiteral("Reject"), this))
    , m_acceptFrameButton(new QPushButton(QStringLiteral("Accept frame"), this))
    , m_rejectFrameButton(new QPushButton(QStringLiteral("Reject frame"), this))
    , m_acceptAllButton(new QPushButton(QStringLiteral("Accept everywhere"), this))
    , m_rejectAllButton(new QPushButton(QStringLiteral("Clear all"), this))
    , m_createClassesCheck(new QCheckBox(QStringLiteral("Create missing classes on accept"), this))
    , m_progress(new QProgressBar(this))
    , m_status(new QLabel(this))
{
    m_taskCombo->addItem(QStringLiteral("Detect (boxes)"), static_cast<int>(ModelTask::Detection));
    m_taskCombo->addItem(QStringLiteral("Segment (masks)"),
                         static_cast<int>(ModelTask::Segmentation));
    m_taskCombo->setToolTip(
        QStringLiteral("Which model runs. Segmentation produces polygons; detection is faster."));

    m_thresholdSlider->setRange(1, 99);
    m_thresholdSlider->setValue(25);
    m_thresholdSlider->setToolTip(
        QStringLiteral("Predictions below this confidence are hidden and cannot be accepted. "
                       "Applies to every frame at once."));

    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setAlternatingRowColors(true);

    m_summary->setStyleSheet(QStringLiteral("color: gray;"));
    m_status->setStyleSheet(QStringLiteral("color: gray;"));
    m_status->setWordWrap(true);

    m_createClassesCheck->setChecked(true);
    m_createClassesCheck->setToolTip(
        QStringLiteral("A detection whose class has no match in your label schema creates one. "
                       "With this off, such predictions are skipped rather than accepted "
                       "unlabelled."));

    m_progress->setVisible(false);
    m_cancelButton->setVisible(false);

    m_acceptAllButton->setToolTip(
        QStringLiteral("Accepts every visible prediction on every frame, as a single undo step."));
    m_rejectAllButton->setToolTip(
        QStringLiteral("Discards every prediction in the project. Annotations are untouched."));

    auto *runRow = new QHBoxLayout;
    runRow->addWidget(m_taskCombo, 1);
    runRow->addWidget(m_runFrameButton);
    runRow->addWidget(m_runAllButton);
    runRow->addWidget(m_cancelButton);

    auto *thresholdRow = new QHBoxLayout;
    thresholdRow->addWidget(new QLabel(QStringLiteral("Min score:"), this));
    thresholdRow->addWidget(m_thresholdSlider, 1);
    thresholdRow->addWidget(m_thresholdLabel);

    auto *buttonGrid = new QGridLayout;
    buttonGrid->addWidget(m_acceptButton, 0, 0);
    buttonGrid->addWidget(m_rejectButton, 0, 1);
    buttonGrid->addWidget(m_acceptFrameButton, 1, 0);
    buttonGrid->addWidget(m_rejectFrameButton, 1, 1);
    buttonGrid->addWidget(m_acceptAllButton, 2, 0);
    buttonGrid->addWidget(m_rejectAllButton, 2, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->addLayout(runRow);
    layout->addLayout(thresholdRow);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_summary);
    layout->addLayout(buttonGrid);
    layout->addWidget(m_createClassesCheck);
    layout->addWidget(m_progress);
    layout->addWidget(m_status);

    connect(m_runFrameButton, &QPushButton::clicked, this,
            &PredictionPanel::runOnFrameRequested);
    connect(m_runAllButton, &QPushButton::clicked, this,
            &PredictionPanel::runOnAllFramesRequested);
    connect(m_cancelButton, &QPushButton::clicked, this, &PredictionPanel::cancelRunRequested);

    connect(m_thresholdSlider, &QSlider::valueChanged, this,
            &PredictionPanel::onThresholdMoved);
    connect(m_list, &QListWidget::itemSelectionChanged, this,
            &PredictionPanel::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &PredictionPanel::onAcceptClicked);

    connect(m_acceptButton, &QPushButton::clicked, this, &PredictionPanel::onAcceptClicked);
    connect(m_rejectButton, &QPushButton::clicked, this, &PredictionPanel::onRejectClicked);
    connect(m_acceptFrameButton, &QPushButton::clicked, this,
            [this] { emit acceptFrameRequested(m_frame); });
    connect(m_rejectFrameButton, &QPushButton::clicked, this,
            [this] { emit rejectFrameRequested(m_frame); });
    connect(m_acceptAllButton, &QPushButton::clicked, this, &PredictionPanel::acceptAllRequested);
    connect(m_rejectAllButton, &QPushButton::clicked, this, &PredictionPanel::rejectAllRequested);

    connect(m_taskCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index >= 0)
            emit taskChanged(static_cast<ModelTask>(m_taskCombo->itemData(index).toInt()));
    });
    connect(m_createClassesCheck, &QCheckBox::toggled, this,
            &PredictionPanel::createMissingClassesChanged);

    onThresholdMoved(m_thresholdSlider->value());
    updateButtons();
    updateSummary();
}

void PredictionPanel::setStore(PredictionStore *store)
{
    if (m_store == store)
        return;

    if (m_store)
        m_store->disconnect(this);

    m_store = store;
    if (m_store) {
        connect(m_store, &PredictionStore::framePredictionsChanged, this, [this](int frame) {
            if (frame == m_frame)
                refresh();
            else
                updateSummary();   // the "everywhere" totals still moved
        });
        connect(m_store, &PredictionStore::storeCleared, this, &PredictionPanel::refresh);
        connect(m_store, &PredictionStore::scoreThresholdChanged, this,
                [this](double threshold) {
                    const int slider = qRound(threshold * 100.0);
                    if (slider != m_thresholdSlider->value()) {
                        QSignalBlocker blocker(m_thresholdSlider);
                        m_thresholdSlider->setValue(slider);
                        m_thresholdLabel->setText(QStringLiteral("%1").arg(threshold, 0, 'f', 2));
                    }
                    refresh();
                });

        m_store->setScoreThreshold(m_thresholdSlider->value() / 100.0);
    }

    refresh();
}

void PredictionPanel::setLabelSchema(const LabelSchema *schema)
{
    m_schema = schema;
    refresh();
}

void PredictionPanel::setCurrentFrame(int frame)
{
    if (m_frame == frame)
        return;
    m_frame = frame;
    refresh();
}

void PredictionPanel::setBusy(bool busy)
{
    m_busy = busy;
    updateButtons();
}

ModelTask PredictionPanel::selectedTask() const
{
    return static_cast<ModelTask>(m_taskCombo->currentData().toInt());
}

bool PredictionPanel::createMissingClasses() const
{
    return m_createClassesCheck->isChecked();
}

double PredictionPanel::scoreThreshold() const
{
    return m_thresholdSlider->value() / 100.0;
}

void PredictionPanel::onThresholdMoved(int value)
{
    const double threshold = value / 100.0;
    m_thresholdLabel->setText(QStringLiteral("%1").arg(threshold, 0, 'f', 2));
    emit scoreThresholdChanged(threshold);
}

void PredictionPanel::refresh()
{
    const QString previous = currentPredictionId();

    m_list->clear();

    if (m_store && m_frame >= 0) {
        for (const Prediction &prediction : m_store->visiblePredictions(m_frame)) {
            const QString shapeKind = prediction.hasPolygon() ? QStringLiteral("mask")
                                                              : QStringLiteral("box");
            auto *item = new QListWidgetItem(
                QStringLiteral("%1  %2  (%3)")
                    .arg(prediction.modelClassName.isEmpty()
                             ? QStringLiteral("unnamed")
                             : prediction.modelClassName)
                    .arg(prediction.score, 0, 'f', 2)
                    .arg(shapeKind),
                m_list);
            item->setData(kPredictionIdRole, prediction.id);

            // Colour it by the class it *would* become, so the user can see at a
            // glance which predictions map onto classes they already have.
            const LabelClass *existing =
                m_schema ? m_schema->findClassByName(prediction.modelClassName) : nullptr;
            item->setIcon(colorSwatch(existing ? existing->color : QColor()));
            item->setToolTip(existing
                                 ? QStringLiteral("Maps to existing class \"%1\".")
                                       .arg(existing->name)
                                 : QStringLiteral("No class named \"%1\" yet — accepting "
                                                  "creates one if that option is on.")
                                       .arg(prediction.modelClassName));

            if (prediction.id == previous)
                m_list->setCurrentItem(item);
        }
    }

    updateSummary();
    updateButtons();
}

void PredictionPanel::updateSummary()
{
    if (!m_store || m_store->isEmpty()) {
        m_summary->setText(QStringLiteral("No predictions."));
        return;
    }

    const int hereVisible = m_frame >= 0 ? m_store->visibleCount(m_frame) : 0;
    const int hereTotal = m_frame >= 0 ? m_store->allPredictions(m_frame).size() : 0;
    const int everywhereVisible = m_store->totalVisibleCount();
    const int everywhereTotal = m_store->totalCount();

    QString text = QStringLiteral("This frame: %1 shown").arg(hereVisible);
    if (hereTotal > hereVisible)
        text += QStringLiteral(" of %1").arg(hereTotal);

    text += QStringLiteral("  •  Project: %1 shown").arg(everywhereVisible);
    if (everywhereTotal > everywhereVisible)
        text += QStringLiteral(" of %1").arg(everywhereTotal);

    text += QStringLiteral(" across %1 frame(s)").arg(m_store->frames().size());
    m_summary->setText(text);
}

QString PredictionPanel::currentPredictionId() const
{
    const QListWidgetItem *item = m_list->currentItem();
    return item ? item->data(kPredictionIdRole).toString() : QString();
}

void PredictionPanel::onSelectionChanged()
{
    updateButtons();
    emit predictionSelected(currentPredictionId());
}

void PredictionPanel::onAcceptClicked()
{
    const QString id = currentPredictionId();
    if (!id.isEmpty())
        emit acceptRequested(m_frame, id);
}

void PredictionPanel::onRejectClicked()
{
    const QString id = currentPredictionId();
    if (!id.isEmpty())
        emit rejectRequested(m_frame, id);
}

void PredictionPanel::updateButtons()
{
    const bool hasStore = m_store != nullptr;
    const bool hasSelection = !currentPredictionId().isEmpty();
    const bool hasOnFrame = hasStore && m_frame >= 0 && m_store->visibleCount(m_frame) > 0;
    const bool hasAnywhere = hasStore && m_store->totalVisibleCount() > 0;

    m_runFrameButton->setEnabled(!m_busy);
    m_runAllButton->setEnabled(!m_busy);
    m_taskCombo->setEnabled(!m_busy);

    m_acceptButton->setEnabled(!m_busy && hasSelection);
    m_rejectButton->setEnabled(!m_busy && hasSelection);
    m_acceptFrameButton->setEnabled(!m_busy && hasOnFrame);
    m_rejectFrameButton->setEnabled(!m_busy && hasOnFrame);
    m_acceptAllButton->setEnabled(!m_busy && hasAnywhere);
    m_rejectAllButton->setEnabled(!m_busy && hasStore && !m_store->isEmpty());
}

void PredictionPanel::onRunStarted(int totalFrames)
{
    setBusy(true);
    m_progress->setVisible(true);
    m_cancelButton->setVisible(true);
    m_progress->setRange(0, qMax(1, totalFrames));
    m_progress->setValue(0);
    m_status->setText(QStringLiteral("Running on %1 frame(s)...").arg(totalFrames));
}

void PredictionPanel::onRunProgress(int done, int total, int detectionsSoFar)
{
    m_progress->setRange(0, qMax(1, total));
    m_progress->setValue(done);
    m_status->setText(QStringLiteral("Frame %1 of %2 — %3 detection(s) so far.")
                          .arg(done)
                          .arg(total)
                          .arg(detectionsSoFar));
}

void PredictionPanel::onRunFinished(int framesProcessed, int totalDetections, const QString &error)
{
    setBusy(false);
    m_progress->setVisible(false);
    m_cancelButton->setVisible(false);

    if (error.isEmpty()) {
        m_status->setText(QStringLiteral("Done — %1 detection(s) across %2 frame(s).")
                              .arg(totalDetections)
                              .arg(framesProcessed));
    } else {
        m_status->setText(QStringLiteral("Stopped after %1 frame(s): %2")
                              .arg(framesProcessed)
                              .arg(error));
    }

    refresh();
}
