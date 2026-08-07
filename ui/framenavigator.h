#ifndef FRAMENAVIGATOR_H
#define FRAMENAVIGATOR_H

#include <QWidget>

class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;

class FrameNavigator : public QWidget
{
    Q_OBJECT
public:
    explicit FrameNavigator(QWidget *parent = nullptr);

    void setFrameCount(int count);
    void setCurrentFrame(int index);

    // Locks out playback and scrubbing. A tracking run advances frames on its own
    // timer and its tracker state assumes sequential frames, so letting the user
    // play or jump at the same time would have the two fight over the playhead.
    void setInteractionEnabled(bool enabled);
    bool isPlaying() const;

    // Sets playback speed from a source's native frame rate (fps). Pass 0
    // (or a source with no inherent rate) to fall back to the default speed.
    void setPlaybackFps(double fps);

signals:
    void frameRequested(int index);

private slots:
    void onSliderValueChanged(int value);
    void onSpinBoxValueChanged(int value);
    void onPlayPauseClicked();
    void onTimerTick();

private:
    void updateLabel();
    void updateEnabledState();
    void requestFrame(int index);

    QSlider *m_slider;
    QSpinBox *m_spinBox;
    QPushButton *m_playButton;
    QLabel *m_countLabel;
    QTimer *m_timer;

    int m_frameCount = 0;
    int m_currentIndex = 0;
    int m_intervalMs;
    bool m_updatingFromCode = false;
    bool m_interactionEnabled = true;
};

#endif // FRAMENAVIGATOR_H
