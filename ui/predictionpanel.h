#ifndef PREDICTIONPANEL_H
#define PREDICTIONPANEL_H

#include "../ai/modelspec.h"

#include <QWidget>

class LabelSchema;
class PredictionStore;

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSlider;

// Review surface for model output: what the model proposed on this frame, how
// confident it was, and the accept/reject decisions.
//
// The panel never mutates anything. Every action is a request the main window
// routes through PredictionController, for the same reason the label panel does
// not delete classes itself — accepting has to be undoable and has to resolve
// classes consistently with every other path.
class PredictionPanel : public QWidget
{
    Q_OBJECT
public:
    explicit PredictionPanel(QWidget *parent = nullptr);

    void setStore(PredictionStore *store);
    void setLabelSchema(const LabelSchema *schema);

    void setCurrentFrame(int frame);
    void refresh();

    // Disables the run controls while something else owns the playhead.
    void setBusy(bool busy);

    ModelTask selectedTask() const;
    bool createMissingClasses() const;
    double scoreThreshold() const;

signals:
    void runOnFrameRequested();
    void runOnAllFramesRequested();
    void cancelRunRequested();

    void acceptRequested(int frame, const QString &predictionId);
    void rejectRequested(int frame, const QString &predictionId);
    void acceptFrameRequested(int frame);
    void rejectFrameRequested(int frame);
    void acceptAllRequested();
    void rejectAllRequested();

    void scoreThresholdChanged(double threshold);
    void taskChanged(ModelTask task);
    void createMissingClassesChanged(bool enabled);

    // Selection moved to a prediction, so the canvas can highlight it.
    void predictionSelected(const QString &predictionId);

public slots:
    void onRunStarted(int totalFrames);
    void onRunProgress(int done, int total, int detectionsSoFar);
    void onRunFinished(int framesProcessed, int totalDetections, const QString &error);

private slots:
    void onThresholdMoved(int value);
    void onSelectionChanged();
    void onAcceptClicked();
    void onRejectClicked();

private:
    void updateButtons();
    void updateSummary();
    QString currentPredictionId() const;

    PredictionStore *m_store = nullptr;
    const LabelSchema *m_schema = nullptr;
    int m_frame = -1;
    bool m_busy = false;

    QComboBox *m_taskCombo;
    QPushButton *m_runFrameButton;
    QPushButton *m_runAllButton;
    QPushButton *m_cancelButton;

    QSlider *m_thresholdSlider;
    QLabel *m_thresholdLabel;

    QListWidget *m_list;
    QLabel *m_summary;

    QPushButton *m_acceptButton;
    QPushButton *m_rejectButton;
    QPushButton *m_acceptFrameButton;
    QPushButton *m_rejectFrameButton;
    QPushButton *m_acceptAllButton;
    QPushButton *m_rejectAllButton;

    QCheckBox *m_createClassesCheck;
    QProgressBar *m_progress;
    QLabel *m_status;
};

#endif // PREDICTIONPANEL_H
