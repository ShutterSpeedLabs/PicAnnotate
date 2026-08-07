#ifndef TRACKPANEL_H
#define TRACKPANEL_H

#include <QWidget>

class LabelSchema;
class TrackTimeline;

class QLabel;
class QListWidget;
class QPushButton;

class TrackPanel : public QWidget
{
    Q_OBJECT
public:
    explicit TrackPanel(QWidget *parent = nullptr);

    // Redraws the track list. Called whenever tracks or the current frame change,
    // since the per-track state shown depends on where the playhead is.
    void refresh(const TrackTimeline *timeline, const LabelSchema *schema, int currentFrame);

    int selectedTrackId() const;

public slots:
    void setStatusText(const QString &text);

    // Swaps Start/Stop availability so the panel reflects whether a run owns the
    // playhead.
    void setTrackingActive(bool active);

signals:
    void startTrackingRequested();
    void stopTrackingRequested();

    void trackSelected(int trackId);
    void frameRequested(int frameIndex);
    void deleteTrackRequested(int trackId);
    void deleteKeyframeRequested(int trackId, int frameIndex);
    void endTrackRequested(int trackId, int frameIndex);
    void toggleOutsideRequested(int trackId, int frameIndex);
    void toggleOccludedRequested(int trackId, int frameIndex);

private slots:
    void onSelectionChanged();
    void onPreviousKeyframeClicked();
    void onNextKeyframeClicked();
    void onDeleteKeyframeClicked();
    void onEndTrackClicked();
    void onOutsideClicked();
    void onOccludedClicked();
    void onDeleteTrackClicked();

private:
    void updateButtonStates();

    QPushButton *m_startButton;
    QPushButton *m_stopButton;
    QLabel *m_statusLabel;

    QListWidget *m_trackList;
    QPushButton *m_prevKeyframeButton;
    QPushButton *m_nextKeyframeButton;
    QPushButton *m_deleteKeyframeButton;
    QPushButton *m_endTrackButton;
    QPushButton *m_outsideButton;
    QPushButton *m_occludedButton;
    QPushButton *m_deleteTrackButton;

    const TrackTimeline *m_timeline = nullptr;
    int m_currentFrame = -1;
};

#endif // TRACKPANEL_H
