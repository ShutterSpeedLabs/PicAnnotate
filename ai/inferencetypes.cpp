#include "inferencetypes.h"

#include <QObject>

QString executionProviderToString(ExecutionProvider provider)
{
    switch (provider) {
    case ExecutionProvider::DirectMl: return QStringLiteral("directml");
    case ExecutionProvider::Cuda:     return QStringLiteral("cuda");
    case ExecutionProvider::Cpu:      break;
    }
    return QStringLiteral("cpu");
}

QString executionProviderDisplayName(ExecutionProvider provider)
{
    switch (provider) {
    case ExecutionProvider::DirectMl: return QObject::tr("DirectML (GPU)");
    case ExecutionProvider::Cuda:     return QObject::tr("CUDA (NVIDIA GPU)");
    case ExecutionProvider::Cpu:      break;
    }
    return QObject::tr("CPU");
}

ExecutionProvider executionProviderFromString(const QString &s)
{
    const QString key = s.trimmed().toLower();
    if (key == QLatin1String("directml") || key == QLatin1String("dml"))
        return ExecutionProvider::DirectMl;
    if (key == QLatin1String("cuda"))
        return ExecutionProvider::Cuda;
    return ExecutionProvider::Cpu;
}
