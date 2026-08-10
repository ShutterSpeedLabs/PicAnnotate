#ifndef DATASETWIZARD_H
#define DATASETWIZARD_H

#include "../io/datasetbuilder.h"

#include <QWizard>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTextEdit;
class QThread;

// "Raw footage in, annotatable dataset out."
//
// Four steps: pick sources, decide how densely to sample them, choose where the
// result goes, then watch it run. The extraction itself happens on a worker
// thread — a two-hour video is minutes of decoding, and a frozen wizard with no
// cancel button is worse than no wizard.
class DatasetWizard : public QWizard
{
    Q_OBJECT
public:
    enum PageId { SourcesPage, SamplingPage, OutputPage, RunPage };

    explicit DatasetWizard(QWidget *parent = nullptr);
    ~DatasetWizard() override;

    // Valid only after the wizard completes successfully.
    const BuildReport &report() const { return m_report; }

    // Where the extracted images landed, for the caller to open as a project.
    QString imageDirectory() const { return m_report.imageDirectory; }

    // Whether the user asked for a detection pass once the project is open.
    bool shouldAutoAnnotate() const;

protected:
    void done(int result) override;

private slots:
    void onAddVideos();
    void onAddFolder();
    void onRemoveSource();
    void onBrowseOutput();
    void onSamplingChanged();
    void onBuildProgress(int sourceIndex, int sourceCount, const QString &sourceName,
                         int framesWritten, int framesExamined);
    void onBuildFinished(const BuildReport &report);
    void onPageChanged(int id);

private:
    QWizardPage *createSourcesPage();
    QWizardPage *createSamplingPage();
    QWizardPage *createOutputPage();
    QWizardPage *createRunPage();

    void addSource(const IngestSource &source);
    void refreshSourceList();
    void refreshEstimate();
    void startBuild();

    BuildOptions collectOptions() const;
    SamplingOptions collectSampling() const;

    QVector<IngestSource> m_sources;
    BuildReport m_report;

    QThread *m_thread = nullptr;
    DatasetBuilder *m_builder = nullptr;
    bool m_running = false;
    bool m_finished = false;

    // Read by the gated pages to decide whether Next/Finish is available.
    bool m_hasSources = false;
    bool m_hasOutputDirectory = false;
    bool m_buildSucceeded = false;

    // Sources page
    QListWidget *m_sourceList;
    QLabel *m_sourceSummary;

    // Sampling page
    QComboBox *m_modeCombo;
    QSpinBox *m_everyNthSpin;
    QDoubleSpinBox *m_targetFpsSpin;
    QSpinBox *m_maxFramesSpin;
    QCheckBox *m_blurCheck;
    QDoubleSpinBox *m_blurSpin;
    QCheckBox *m_similarCheck;
    QDoubleSpinBox *m_similarSpin;
    QCheckBox *m_resizeCheck;
    QSpinBox *m_resizeWidthSpin;
    QSpinBox *m_resizeHeightSpin;
    QComboBox *m_formatCombo;
    QSpinBox *m_qualitySpin;
    QLabel *m_estimateLabel;

    // Output page
    QLineEdit *m_outputEdit;
    QCheckBox *m_autoAnnotateCheck;

    // Run page
    QProgressBar *m_progressBar;
    QLabel *m_progressLabel;
    QTextEdit *m_summary;
    QPushButton *m_cancelBuildButton;
};

#endif // DATASETWIZARD_H
