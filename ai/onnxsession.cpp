#include "onnxsession.h"

#include <QFileInfo>
#include <QObject>

#ifdef HAVE_ONNXRUNTIME

#include <onnxruntime_cxx_api.h>

#ifdef HAVE_ORT_DIRECTML
#include <dml_provider_factory.h>
#endif

#include <string>
#include <vector>

namespace {

// One environment per process. ORT requires it to outlive every session it
// created, and creating a second one per model wastes its thread pools.
Ort::Env &ortEnv()
{
    static Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "PicAnnotate");
    return env;
}

#ifdef _WIN32
using OrtPathString = std::wstring;
OrtPathString toOrtPath(const QString &path) { return path.toStdWString(); }
#else
using OrtPathString = std::string;
OrtPathString toOrtPath(const QString &path) { return path.toStdString(); }
#endif

QVector<qint64> toShapeVector(const std::vector<int64_t> &shape)
{
    QVector<qint64> result;
    result.reserve(static_cast<int>(shape.size()));
    for (int64_t extent : shape)
        result.append(static_cast<qint64>(extent));
    return result;
}

// Adds the requested provider to `options`, returning what was actually
// registered. A missing provider is not an error: the model still runs, just on
// the CPU, and the caller reports that rather than refusing to work.
ExecutionProvider applyProvider(Ort::SessionOptions &options, ExecutionProvider requested)
{
    switch (requested) {
    case ExecutionProvider::DirectMl:
#ifdef HAVE_ORT_DIRECTML
        // DirectML cannot use ORT's memory-pattern planner and needs the
        // sequential executor; both are hard requirements, not tuning.
        options.DisableMemPattern();
        options.SetExecutionMode(ORT_SEQUENTIAL);
        if (OrtSessionOptionsAppendExecutionProvider_DML(options, 0) == nullptr)
            return ExecutionProvider::DirectMl;
#endif
        break;

    case ExecutionProvider::Cuda:
#ifdef HAVE_ORT_CUDA
        try {
            OrtCUDAProviderOptions cudaOptions;
            options.AppendExecutionProvider_CUDA(cudaOptions);
            return ExecutionProvider::Cuda;
        } catch (const Ort::Exception &) {
            // No CUDA device or no matching driver: fall through to CPU.
        }
#endif
        break;

    case ExecutionProvider::Cpu:
        break;
    }

    return ExecutionProvider::Cpu;
}

} // namespace

#endif // HAVE_ONNXRUNTIME

struct OnnxSession::Private
{
    QString path;
    ExecutionProvider requested = ExecutionProvider::Cpu;
    ExecutionProvider active = ExecutionProvider::Cpu;
    int intraOpThreads = 0;
    bool loaded = false;

    QStringList inputNames;
    QStringList outputNames;
    QVector<QVector<qint64>> inputShapes;
    QVector<QVector<qint64>> outputShapes;

#ifdef HAVE_ONNXRUNTIME
    std::unique_ptr<Ort::Session> session;

    // ORT hands out allocator-owned name strings; these are the copies the
    // const char* arrays passed to Run() actually point at.
    std::vector<std::string> inputNameStore;
    std::vector<std::string> outputNameStore;
#endif
};

OnnxSession::OnnxSession()
    : d(std::make_unique<Private>())
{
}

OnnxSession::~OnnxSession() = default;

bool OnnxSession::isLoaded() const
{
    return d->loaded;
}

QString OnnxSession::modelPath() const
{
    return d->path;
}

ExecutionProvider OnnxSession::activeProvider() const
{
    return d->active;
}

QStringList OnnxSession::inputNames() const
{
    return d->inputNames;
}

QStringList OnnxSession::outputNames() const
{
    return d->outputNames;
}

QVector<QVector<qint64>> OnnxSession::inputShapes() const
{
    return d->inputShapes;
}

QVector<QVector<qint64>> OnnxSession::outputShapes() const
{
    return d->outputShapes;
}

void OnnxSession::setIntraOpThreads(int threads)
{
    d->intraOpThreads = threads > 0 ? threads : 0;
}

int OnnxSession::intraOpThreads() const
{
    return d->intraOpThreads;
}

void OnnxSession::unload()
{
#ifdef HAVE_ONNXRUNTIME
    d->session.reset();
    d->inputNameStore.clear();
    d->outputNameStore.clear();
#endif
    d->loaded = false;
    d->path.clear();
    d->inputNames.clear();
    d->outputNames.clear();
    d->inputShapes.clear();
    d->outputShapes.clear();
}

bool OnnxSession::load(const QString &modelPath, ExecutionProvider requested, QString *error)
{
#ifndef HAVE_ONNXRUNTIME
    Q_UNUSED(modelPath);
    Q_UNUSED(requested);
    if (error) {
        *error = QObject::tr("This build has no ONNX Runtime, so ONNX models cannot be run. "
                             "See docs/AI_SETUP.md for how to rebuild with it.");
    }
    return false;
#else
    if (d->loaded && d->path == modelPath && d->requested == requested)
        return true;

    if (!QFileInfo::exists(modelPath)) {
        if (error)
            *error = QObject::tr("Model file not found: %1").arg(modelPath);
        return false;
    }

    unload();

    try {
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        if (d->intraOpThreads > 0)
            options.SetIntraOpNumThreads(d->intraOpThreads);

        const ExecutionProvider active = applyProvider(options, requested);

        const OrtPathString nativePath = toOrtPath(modelPath);
        d->session = std::make_unique<Ort::Session>(ortEnv(), nativePath.c_str(), options);

        Ort::AllocatorWithDefaultOptions allocator;

        const size_t inputCount = d->session->GetInputCount();
        d->inputNameStore.reserve(inputCount);
        for (size_t i = 0; i < inputCount; ++i) {
            Ort::AllocatedStringPtr name = d->session->GetInputNameAllocated(i, allocator);
            d->inputNameStore.emplace_back(name.get());
            d->inputNames.append(QString::fromUtf8(name.get()));
            d->inputShapes.append(toShapeVector(
                d->session->GetInputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape()));
        }

        const size_t outputCount = d->session->GetOutputCount();
        d->outputNameStore.reserve(outputCount);
        for (size_t i = 0; i < outputCount; ++i) {
            Ort::AllocatedStringPtr name = d->session->GetOutputNameAllocated(i, allocator);
            d->outputNameStore.emplace_back(name.get());
            d->outputNames.append(QString::fromUtf8(name.get()));
            d->outputShapes.append(toShapeVector(
                d->session->GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo().GetShape()));
        }

        d->path = modelPath;
        d->requested = requested;
        d->active = active;
        d->loaded = true;
        return true;
    } catch (const Ort::Exception &e) {
        unload();
        if (error) {
            *error = QObject::tr("Could not load %1:\n%2")
                         .arg(QFileInfo(modelPath).fileName(), QString::fromUtf8(e.what()));
        }
        return false;
    } catch (const std::exception &e) {
        unload();
        if (error) {
            *error = QObject::tr("Could not load %1:\n%2")
                         .arg(QFileInfo(modelPath).fileName(), QString::fromUtf8(e.what()));
        }
        return false;
    }
#endif
}

bool OnnxSession::run(const QVector<NamedTensor> &inputs, const QStringList &wantedOutputs,
                      QVector<Tensor> *outputs, QString *error)
{
#ifndef HAVE_ONNXRUNTIME
    Q_UNUSED(inputs);
    Q_UNUSED(wantedOutputs);
    Q_UNUSED(outputs);
    if (error)
        *error = QObject::tr("This build has no ONNX Runtime.");
    return false;
#else
    if (!d->loaded || !d->session) {
        if (error)
            *error = QObject::tr("No model is loaded.");
        return false;
    }
    if (!outputs) {
        if (error)
            *error = QObject::tr("No output buffer was supplied.");
        return false;
    }
    outputs->clear();

    try {
        const Ort::MemoryInfo memoryInfo =
            Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        std::vector<std::string> inputNameStore;
        std::vector<const char *> inputNamePtrs;
        std::vector<Ort::Value> inputValues;
        inputNameStore.reserve(inputs.size());
        inputNamePtrs.reserve(inputs.size());
        inputValues.reserve(inputs.size());

        for (const NamedTensor &named : inputs) {
            const Tensor &t = named.tensor;
            if (!t.isValid()) {
                if (error) {
                    *error = QObject::tr("Input \"%1\" has shape %2, which does not match "
                                         "the %3 values supplied.")
                                 .arg(named.name, t.shapeString())
                                 .arg(t.type == Tensor::Type::Float32 ? t.f32.size()
                                      : t.type == Tensor::Type::Int64 ? t.i64.size()
                                                                      : t.i32.size());
                }
                return false;
            }

            inputNameStore.emplace_back(named.name.toStdString());
            inputNamePtrs.push_back(inputNameStore.back().c_str());

            std::vector<int64_t> shape;
            shape.reserve(t.shape.size());
            for (qint64 extent : t.shape)
                shape.push_back(static_cast<int64_t>(extent));

            // ORT wants a mutable pointer but never writes through it for an
            // input. Casting away const here avoids copying every image blob.
            switch (t.type) {
            case Tensor::Type::Float32:
                inputValues.push_back(Ort::Value::CreateTensor<float>(
                    memoryInfo, const_cast<float *>(t.f32.data()), t.f32.size(),
                    shape.data(), shape.size()));
                break;
            case Tensor::Type::Int64:
                inputValues.push_back(Ort::Value::CreateTensor<int64_t>(
                    memoryInfo, reinterpret_cast<int64_t *>(const_cast<qint64 *>(t.i64.data())),
                    t.i64.size(), shape.data(), shape.size()));
                break;
            case Tensor::Type::Int32:
                inputValues.push_back(Ort::Value::CreateTensor<int32_t>(
                    memoryInfo, reinterpret_cast<int32_t *>(const_cast<qint32 *>(t.i32.data())),
                    t.i32.size(), shape.data(), shape.size()));
                break;
            }
        }

        // An empty wanted-list means "everything the graph produces".
        QStringList requestedNames = wantedOutputs;
        if (requestedNames.isEmpty())
            requestedNames = d->outputNames;

        std::vector<std::string> outputNameStore;
        std::vector<const char *> outputNamePtrs;
        outputNameStore.reserve(requestedNames.size());
        outputNamePtrs.reserve(requestedNames.size());
        for (const QString &name : requestedNames) {
            outputNameStore.emplace_back(name.toStdString());
            outputNamePtrs.push_back(outputNameStore.back().c_str());
        }

        std::vector<Ort::Value> results = d->session->Run(
            Ort::RunOptions{nullptr}, inputNamePtrs.data(), inputValues.data(),
            inputValues.size(), outputNamePtrs.data(), outputNamePtrs.size());

        outputs->reserve(static_cast<int>(results.size()));
        for (Ort::Value &value : results) {
            Tensor tensor;
            if (!value.IsTensor()) {
                // Sequence and map outputs exist in ONNX but none of the models
                // here produce them; an empty tensor keeps positions aligned.
                outputs->append(tensor);
                continue;
            }

            const Ort::TensorTypeAndShapeInfo info = value.GetTensorTypeAndShapeInfo();
            tensor.shape = toShapeVector(info.GetShape());
            const size_t count = info.GetElementCount();

            switch (info.GetElementType()) {
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT: {
                const float *data = value.GetTensorData<float>();
                tensor.type = Tensor::Type::Float32;
                tensor.f32.assign(data, data + count);
                break;
            }
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64: {
                const int64_t *data = value.GetTensorData<int64_t>();
                tensor.type = Tensor::Type::Int64;
                tensor.i64.assign(data, data + count);
                break;
            }
            case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32: {
                const int32_t *data = value.GetTensorData<int32_t>();
                tensor.type = Tensor::Type::Int32;
                tensor.i32.assign(data, data + count);
                break;
            }
            default:
                if (error) {
                    *error = QObject::tr("Output tensor has an element type this build does "
                                         "not read (only float32, int32 and int64 are handled).");
                }
                return false;
            }

            outputs->append(std::move(tensor));
        }

        return true;
    } catch (const Ort::Exception &e) {
        if (error)
            *error = QObject::tr("Inference failed: %1").arg(QString::fromUtf8(e.what()));
        outputs->clear();
        return false;
    } catch (const std::exception &e) {
        if (error)
            *error = QObject::tr("Inference failed: %1").arg(QString::fromUtf8(e.what()));
        outputs->clear();
        return false;
    }
#endif
}
