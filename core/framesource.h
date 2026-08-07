#ifndef FRAMESOURCE_H
#define FRAMESOURCE_H

#include <QImage>
#include <QSize>
#include <QString>

class FrameSource
{
public:
    virtual ~FrameSource() = default;

    virtual int frameCount() const = 0;
    virtual QImage frameAt(int index) const = 0;
    virtual QString frameLabel(int index) const = 0;

    // Frame dimensions without paying for a full decode where the source can
    // answer more cheaply. The default falls back to decoding.
    virtual QSize frameSize(int index) const { return frameAt(index).size(); }

    // Absolute path of the file backing a frame, or an empty string when frames
    // do not map to individual files (e.g. video). Exporters use it to copy
    // images instead of re-encoding them.
    virtual QString framePath(int index) const { Q_UNUSED(index); return QString(); }

    // Native playback rate in frames per second, or 0 if the source has no
    // inherent rate (e.g. an image folder).
    virtual double frameRate() const { return 0.0; }
};

#endif // FRAMESOURCE_H
