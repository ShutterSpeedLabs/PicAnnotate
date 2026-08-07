#include "framenavigator.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QtGlobal>

namespace {
constexpr int kDefaultPlaybackIntervalMs = 100;
}

FrameNavigator::FrameNavigator(QWidget *parent)
    : QWidget(parent)
    , m_slider(new QSlider(Qt::Horizontal, this))
    , m_spinBox(new QSpinBox(this))
    , m_playButton(new QPushButton("Play", this))
    , m_countLabel(new QLabel(this))
    , m_timer(new QTimer(this))
    , m_intervalMs(kDefaultPlaybackIntervalMs)
{
    auto *layout = new QHBoxLayout(this);
    layout->addWidget(m_playButton);
    layout->addWidget(m_slider, 1);
    layout->addWidget(m_spinBox);
    layout->addWidget(m_countLabel);

    m_slider->setEnabled(false);
    m_spinBox->setEnabled(false);
    m_playButton->setEnabled(false);
    updateLabel();

    connect(m_slider, &QSlider::valueChanged, this, &FrameNavigator::onSliderValueChanged);
    connect(m_spinBox, QOverload<int>::of(&QSpinBox::valueChanged), this, &FrameNavigator::onSpinBoxValueChanged);
    connect(m_playButton, &QPushButton::clicked, this, &FrameNavigator::onPlayPauseClicked);
    connect(m_timer, &QTimer::timeout, this, &FrameNavigator::onTimerTick);
}

void FrameNavigator::setFrameCount(int count)
{
    m_frameCount = count;
    m_timer->stop();
    m_playButton->setText("Play");

    m_updatingFromCode = true;
    m_slider->setRange(0, qMax(0, count - 1));
    m_spinBox->setRange(0, qMax(0, count - 1));
    m_updatingFromCode = false;

    updateEnabledState();
    updateLabel();
}

bool FrameNavigator::isPlaying() const
{
    return m_timer->isActive();
}

void FrameNavigator::setInteractionEnabled(bool enabled)
{
    m_interactionEnabled = enabled;

    if (!enabled && m_timer->isActive()) {
        m_timer->stop();
        m_playButton->setText("Play");
    }
    updateEnabledState();
}

void FrameNavigator::updateEnabledState()
{
    // Both conditions have to hold, so neither setFrameCount() nor
    // setInteractionEnabled() can re-enable controls the other one disabled.
    const bool usable = m_frameCount > 0 && m_interactionEnabled;
    m_slider->setEnabled(usable);
    m_spinBox->setEnabled(usable);
    m_playButton->setEnabled(usable);
}

void FrameNavigator::setCurrentFrame(int index)
{
    m_currentIndex = index;

    m_updatingFromCode = true;
    m_slider->setValue(index);
    m_spinBox->setValue(index);
    m_updatingFromCode = false;

    updateLabel();
}

void FrameNavigator::onSliderValueChanged(int value)
{
    if (!m_updatingFromCode)
        requestFrame(value);
}

void FrameNavigator::onSpinBoxValueChanged(int value)
{
    if (!m_updatingFromCode)
        requestFrame(value);
}

void FrameNavigator::onPlayPauseClicked()
{
    if (m_timer->isActive()) {
        m_timer->stop();
        m_playButton->setText("Play");
        return;
    }

    if (m_currentIndex + 1 >= m_frameCount)
        return;

    m_timer->start(m_intervalMs);
    m_playButton->setText("Pause");
}

void FrameNavigator::setPlaybackFps(double fps)
{
    m_intervalMs = fps > 0.0 ? qMax(1, qRound(1000.0 / fps)) : kDefaultPlaybackIntervalMs;
    if (m_timer->isActive())
        m_timer->setInterval(m_intervalMs);
}

void FrameNavigator::onTimerTick()
{
    if (m_currentIndex + 1 >= m_frameCount) {
        m_timer->stop();
        m_playButton->setText("Play");
        return;
    }
    requestFrame(m_currentIndex + 1);
}

void FrameNavigator::updateLabel()
{
    m_countLabel->setText(m_frameCount > 0
        ? QString("Frame %1 / %2").arg(m_currentIndex + 1).arg(m_frameCount)
        : "No frames");
}

void FrameNavigator::requestFrame(int index)
{
    emit frameRequested(index);
}
