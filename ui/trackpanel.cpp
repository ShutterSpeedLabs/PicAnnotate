#include "trackpanel.h"

#include "../core/labelschema.h"
#include "../core/tracktimeline.h"

#include <QGridLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
constexpr int kTrackIdRole = Qt::UserRole + 1;
}

TrackPanel::TrackPanel(QWidget *parent)
    : QWidget(parent)
    , m_startButton(new QPushButton("Track Selected Shape", this))
    , m_stopButton(new QPushButton("Stop Tracking", this))
    , m_statusLabel(new QLabel("Idle", this))
    , m_trackList(new QListWidget(this))
    , m_prevKeyframeButton(new QPushButton("< Key", this))
    , m_nextKeyframeButton(new QPushButton("Key >", this))
    , m_deleteKeyframeButton(new QPushButton("Delete Keyframe", this))
    , m_endTrackButton(new QPushButton("End Track Here", this))
    , m_outsideButton(new QPushButton("Toggle Absent", this))
    , m_occludedButton(new QPushButton("Toggle Occluded", this))
    , m_deleteTrackButton(new QPushButton("Delete Track", this))
{
    m_trackList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_prevKeyframeButton->setToolTip(QStringLiteral("Jump to the previous keyframe of this track"));
    m_nextKeyframeButton->setToolTip(QStringLiteral("Jump to the next keyframe of this track"));
    m_outsideButton->setToolTip(
        QStringLiteral("Mark the object absent from this frame until the next keyframe"));
    m_occludedButton->setToolTip(QStringLiteral("Mark the object present but hidden on this frame"));
    m_endTrackButton->setToolTip(QStringLiteral("Drop every keyframe from this frame onwards"));

    auto *keyframeRow = new QGridLayout;
    keyframeRow->addWidget(m_prevKeyframeButton, 0, 0);
    keyframeRow->addWidget(m_nextKeyframeButton, 0, 1);
    keyframeRow->addWidget(m_deleteKeyframeButton, 1, 0, 1, 2);
    keyframeRow->addWidget(m_outsideButton, 2, 0);
    keyframeRow->addWidget(m_occludedButton, 2, 1);
    keyframeRow->addWidget(m_endTrackButton, 3, 0, 1, 2);
    keyframeRow->addWidget(m_deleteTrackButton, 4, 0, 1, 2);

    // This panel carries a lot of controls. Without the scroll area their
    // combined minimum height becomes the dock's minimum, which in turn forces a
    // window taller than the screen on a laptop display.
    auto *content = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->addWidget(m_startButton);
    contentLayout->addWidget(m_stopButton);
    contentLayout->addWidget(m_statusLabel);
    contentLayout->addWidget(new QLabel(QStringLiteral("Tracks"), content));
    contentLayout->addWidget(m_trackList, 1);
    contentLayout->addLayout(keyframeRow);

    m_trackList->setMinimumHeight(70);

    auto *scrollArea = new QScrollArea(this);
    scrollArea->setWidget(content);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(scrollArea);

    connect(m_startButton, &QPushButton::clicked, this, &TrackPanel::startTrackingRequested);
    connect(m_stopButton, &QPushButton::clicked, this, &TrackPanel::stopTrackingRequested);
    connect(m_trackList, &QListWidget::itemSelectionChanged, this, &TrackPanel::onSelectionChanged);
    connect(m_prevKeyframeButton, &QPushButton::clicked, this, &TrackPanel::onPreviousKeyframeClicked);
    connect(m_nextKeyframeButton, &QPushButton::clicked, this, &TrackPanel::onNextKeyframeClicked);
    connect(m_deleteKeyframeButton, &QPushButton::clicked, this, &TrackPanel::onDeleteKeyframeClicked);
    connect(m_endTrackButton, &QPushButton::clicked, this, &TrackPanel::onEndTrackClicked);
    connect(m_outsideButton, &QPushButton::clicked, this, &TrackPanel::onOutsideClicked);
    connect(m_occludedButton, &QPushButton::clicked, this, &TrackPanel::onOccludedClicked);
    connect(m_deleteTrackButton, &QPushButton::clicked, this, &TrackPanel::onDeleteTrackClicked);

    setTrackingActive(false);
    updateButtonStates();
}

void TrackPanel::setStatusText(const QString &text)
{
    m_statusLabel->setText(text);
}

void TrackPanel::setTrackingActive(bool active)
{
    m_startButton->setEnabled(!active);
    m_stopButton->setEnabled(active);
}

int TrackPanel::selectedTrackId() const
{
    const QListWidgetItem *item = m_trackList->currentItem();
    return item ? item->data(kTrackIdRole).toInt() : -1;
}

void TrackPanel::refresh(const TrackTimeline *timeline, const LabelSchema *schema, int currentFrame)
{
    m_timeline = timeline;
    m_currentFrame = currentFrame;

    const int previousSelection = selectedTrackId();

    // Rebuilding the list fires selection changes; suppress them so the caller
    // does not see a spurious trackSelected() for the row that just vanished.
    const QSignalBlocker blocker(m_trackList);
    m_trackList->clear();

    if (timeline) {
        for (int trackId : timeline->trackIds()) {
            const AnnoTrack *track = timeline->track(trackId);
            if (!track || track->isEmpty())
                continue;

            const LabelClass *label = schema ? schema->findClass(track->labelId()) : nullptr;
            const bool liveHere = track->shapeAt(currentFrame).has_value();
            const bool keyHere = track->hasKeyframeAt(currentFrame);

            QString state;
            if (keyHere)
                state = QStringLiteral(" [key]");
            else if (liveHere)
                state = QStringLiteral(" [interp]");

            const TrackKeyframe keyframe = track->keyframes().value(currentFrame);
            if (keyHere && keyframe.outside)
                state += QStringLiteral(" absent");
            else if (keyHere && keyframe.occluded)
                state += QStringLiteral(" occluded");

            auto *item = new QListWidgetItem(
                QStringLiteral("#%1 %2 · %3 · frames %4-%5 · %6 keys%7")
                    .arg(trackId)
                    .arg(label ? label->name : QStringLiteral("Unlabeled"))
                    .arg(shapeTypeToString(track->type()))
                    .arg(track->firstFrame())
                    .arg(track->lastFrame())
                    .arg(track->keyframeCount())
                    .arg(state),
                m_trackList);
            item->setData(kTrackIdRole, trackId);

            if (label && label->color.isValid())
                item->setForeground(label->color);
            if (!liveHere)
                item->setToolTip(QStringLiteral("Not present on frame %1").arg(currentFrame));

            if (trackId == previousSelection)
                m_trackList->setCurrentItem(item);
        }
    }

    updateButtonStates();
}

void TrackPanel::updateButtonStates()
{
    const int trackId = selectedTrackId();
    const AnnoTrack *track = m_timeline ? m_timeline->track(trackId) : nullptr;

    const bool hasTrack = track != nullptr;
    const bool keyHere = hasTrack && track->hasKeyframeAt(m_currentFrame);
    const bool liveHere = hasTrack && track->shapeAt(m_currentFrame).has_value();

    m_prevKeyframeButton->setEnabled(hasTrack && track->keyframeAtOrBefore(m_currentFrame - 1) >= 0);
    m_nextKeyframeButton->setEnabled(hasTrack && track->keyframeAfter(m_currentFrame) >= 0);
    // Removing the only keyframe would delete the track by the back door.
    m_deleteKeyframeButton->setEnabled(keyHere && track->keyframeCount() > 1);
    m_endTrackButton->setEnabled(hasTrack && m_currentFrame > track->firstFrame());
    m_outsideButton->setEnabled(hasTrack && (liveHere || keyHere));
    m_occludedButton->setEnabled(hasTrack && (liveHere || keyHere));
    m_deleteTrackButton->setEnabled(hasTrack);
}

void TrackPanel::onSelectionChanged()
{
    updateButtonStates();
    const int trackId = selectedTrackId();
    if (trackId >= 0)
        emit trackSelected(trackId);
}

void TrackPanel::onPreviousKeyframeClicked()
{
    const AnnoTrack *track = m_timeline ? m_timeline->track(selectedTrackId()) : nullptr;
    if (!track)
        return;
    const int frame = track->keyframeAtOrBefore(m_currentFrame - 1);
    if (frame >= 0)
        emit frameRequested(frame);
}

void TrackPanel::onNextKeyframeClicked()
{
    const AnnoTrack *track = m_timeline ? m_timeline->track(selectedTrackId()) : nullptr;
    if (!track)
        return;
    const int frame = track->keyframeAfter(m_currentFrame);
    if (frame >= 0)
        emit frameRequested(frame);
}

void TrackPanel::onDeleteKeyframeClicked()
{
    const int trackId = selectedTrackId();
    if (trackId >= 0)
        emit deleteKeyframeRequested(trackId, m_currentFrame);
}

void TrackPanel::onEndTrackClicked()
{
    const int trackId = selectedTrackId();
    if (trackId >= 0)
        emit endTrackRequested(trackId, m_currentFrame);
}

void TrackPanel::onOutsideClicked()
{
    const int trackId = selectedTrackId();
    if (trackId >= 0)
        emit toggleOutsideRequested(trackId, m_currentFrame);
}

void TrackPanel::onOccludedClicked()
{
    const int trackId = selectedTrackId();
    if (trackId >= 0)
        emit toggleOccludedRequested(trackId, m_currentFrame);
}

void TrackPanel::onDeleteTrackClicked()
{
    const int trackId = selectedTrackId();
    if (trackId >= 0)
        emit deleteTrackRequested(trackId);
}
