#ifndef ONNXSESSION_H
#define ONNXSESSION_H

#include "inferencetypes.h"
#include "tensor.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

// A loaded ONNX graph, ready to be run.
//
// ONNX Runtime's headers are kept behind a pimpl on purpose: they are large,
// they are only present when the build found the SDK, and letting them into the
// public header would force every translation unit that mentions a segmenter to
// find them too. The API below stays the same whether or not the build has ORT —
// without it, load() simply fails with an explanation.
//
// Not thread-safe. ORT sessions themselves are, but the cached name buffers here
// are not, and the engines each own their session anyway.
class OnnxSession
{
public:
    OnnxSession();
    ~OnnxSession();

    OnnxSession(const OnnxSession &) = delete;
    OnnxSession &operator=(const OnnxSession &) = delete;

    // Loads `modelPath`, asking for `requested` and quietly falling back to CPU
    // if that provider is not in the runtime. Check activeProvider() to find out
    // what was actually used. Reloading a session that is already on this exact
    // path with this exact provider is a no-op.
    bool load(const QString &modelPath, ExecutionProvider requested, QString *error);
    void unload();

    bool isLoaded() const;
    QString modelPath() const;
    ExecutionProvider activeProvider() const;

    QStringList inputNames() const;
    QStringList outputNames() const;

    // Declared input shapes, with -1 for dimensions the graph leaves dynamic.
    // The engines use these to tell a 1024x1024 SAM encoder from a 640x640 one
    // instead of assuming.
    QVector<QVector<qint64>> inputShapes() const;
    QVector<QVector<qint64>> outputShapes() const;

    // Runs the graph. `outputs` comes back in the order `wantedOutputs` asked
    // for; passing an empty list fetches every output in graph order.
    //
    // Returns false rather than throwing: ORT signals everything with C++
    // exceptions, and a throw crossing a Qt slot boundary terminates the app.
    bool run(const QVector<NamedTensor> &inputs, const QStringList &wantedOutputs,
             QVector<Tensor> *outputs, QString *error);

    // Number of threads used for CPU inference. 0 lets ORT decide. Set before
    // load(); changing it afterwards has no effect until the next load.
    void setIntraOpThreads(int threads);
    int intraOpThreads() const;

private:
    struct Private;
    std::unique_ptr<Private> d;
};

#endif // ONNXSESSION_H
