#include "matte.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>

#include "key.h"

namespace matte {

namespace {

// The models are the ONNX exports of BiRefNet (MIT) published with rembg.
const char kReleases[] = "https://github.com/danielgatis/rembg/releases/download/v0.0.0/";

Model make(const char *id, const char *label, const char *file, const char *remote,
           const char *sha256, qint64 bytes) {
    Model model;
    model.id = QString::fromLatin1(id);
    model.label = QString::fromLatin1(label);
    model.file = QString::fromLatin1(file);
    model.url = QUrl(QString::fromLatin1(kReleases) + QString::fromLatin1(remote));
    model.sha256 = sha256;
    model.bytes = bytes;
    return model;
}

// What the networks were trained with, per channel.
const float kMean[3] = {0.485f, 0.456f, 0.406f};
const float kDeviation[3] = {0.229f, 0.224f, 0.225f};

}  // namespace

const Model &fast() {
    static const Model model = make(
        "fast", "Fast", "birefnet-general-lite.onnx", "BiRefNet-general-bb_swin_v1_tiny-epoch_232.onnx",
        "5600024376f572a557870a5eb0afb1e5961636bef4e1e22132025467d0f03333", 224005088);
    return model;
}

const Model &best() {
    static const Model model = make(
        "best", "Best", "birefnet-general.onnx", "BiRefNet-general-epoch_244.onnx",
        "58f621f00f5d756097615970a88a791584600dcf7c45b18a0a6267535a1ebd3c", 972666916);
    return model;
}

const Model *byId(const QString &id) {
    for (const Model *model : {&fast(), &best()})
        if (model->id == id)
            return model;
    return nullptr;
}

QString modelDir() {
    const QString chosen = qEnvironmentVariable("OMALIFT_MODEL_DIR");
    if (!chosen.isEmpty())
        return chosen;
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/omalift/models");
}

QString modelPath(const Model &model) {
    return QDir(modelDir()).filePath(model.file);
}

// A download is only moved into place once its checksum matches, so a file
// of the right size here is a whole one.
bool installed(const Model &model) {
    const QFileInfo file(modelPath(model));
    return file.isFile() && file.size() == model.bytes;
}

QString sizeLabel(const Model &model) {
    return QStringLiteral("%1 MB").arg(qRound64(model.bytes / (1024.0 * 1024.0)));
}

std::vector<float> toTensor(const QImage &image) {
    const QImage square = image.convertToFormat(QImage::Format_RGB32)
                              .scaled(kSide, kSide, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    int brightest = 1;
    for (int y = 0; y < kSide; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(square.constScanLine(y));
        for (int x = 0; x < kSide; ++x)
            brightest = qMax(brightest, qMax(qRed(line[x]), qMax(qGreen(line[x]), qBlue(line[x]))));
    }

    // Scaled so the photo's own brightest value is 1, as the models' authors
    // do, which evens out an underexposed photo before it is looked at.
    const int plane = kSide * kSide;
    std::vector<float> tensor(3 * plane);
    for (int y = 0; y < kSide; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(square.constScanLine(y));
        for (int x = 0; x < kSide; ++x) {
            const int at = y * kSide + x;
            const int values[3] = {qRed(line[x]), qGreen(line[x]), qBlue(line[x])};
            for (int c = 0; c < 3; ++c)
                tensor[c * plane + at] = (float(values[c]) / brightest - kMean[c]) / kDeviation[c];
        }
    }
    return tensor;
}

QImage toMask(const float *scores, int width, int height, const QSize &size) {
    if (!scores || width <= 0 || height <= 0)
        return QImage();

    // Each score becomes a likelihood from 0 to 1, then the whole mask is
    // stretched so its surest "person" is white and surest "background" black.
    const int count = width * height;
    std::vector<float> likely(count);
    float lowest = 1.0f;
    float highest = 0.0f;
    for (int i = 0; i < count; ++i) {
        likely[i] = 1.0f / (1.0f + std::exp(-scores[i]));
        lowest = std::min(lowest, likely[i]);
        highest = std::max(highest, likely[i]);
    }
    const float range = std::max(1e-6f, highest - lowest);

    QImage mask(width, height, QImage::Format_Grayscale8);
    for (int y = 0; y < height; ++y) {
        uchar *line = mask.scanLine(y);
        for (int x = 0; x < width; ++x)
            line[x] = uchar(std::lround(255.0f * (likely[y * width + x] - lowest) / range));
    }
    return mask.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

QImage withMask(const QImage &image, const QImage &mask) {
    QImage out = image.convertToFormat(QImage::Format_ARGB32);
    if (out.isNull() || mask.isNull())
        return out;
    const QImage fitted = (mask.size() == out.size() ? mask
                               : mask.scaled(out.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation))
                              .convertToFormat(QImage::Format_Grayscale8);
    for (int y = 0; y < out.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(out.scanLine(y));
        const uchar *alpha = fitted.constScanLine(y);
        for (int x = 0; x < out.width(); ++x) {
            const int a = alpha[x] * qAlpha(line[x]) / 255;
            line[x] = a == 0 ? 0 : qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), a);
        }
    }
    return out;
}

QImage cutout(const QImage &image, const QImage &mask, const QRectF &crop) {
    if (image.isNull() || mask.isNull())
        return QImage();
    const QRect area = QRectF(crop.x() * image.width(), crop.y() * image.height(),
                              crop.width() * image.width(), crop.height() * image.height())
                           .toRect()
                           .intersected(image.rect());
    if (area.isEmpty())
        return QImage();
    const QImage lifted = withMask(image, mask).copy(area);
    const QRect bounds = key::subjectBounds(lifted);
    return bounds.isEmpty() ? QImage() : lifted.copy(bounds);
}

struct Session::Impl {
    // One environment for the life of the program, as ONNX Runtime asks.
    static Ort::Env &env() {
        static Ort::Env shared(ORT_LOGGING_LEVEL_ERROR, "omalift");
        return shared;
    }
    std::unique_ptr<Ort::Session> session;
};

Session::Session() : d(std::make_unique<Impl>()) {}
Session::~Session() = default;

bool Session::open(const QString &path, QString *error) {
    try {
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        d->session = std::make_unique<Ort::Session>(Impl::env(), path.toLocal8Bit().constData(), options);
        return true;
    } catch (const Ort::Exception &problem) {
        d->session.reset();
        if (error)
            *error = QString::fromUtf8(problem.what());
        return false;
    }
}

QImage Session::mask(const QImage &image, QString *error) {
    if (!d->session || image.isNull()) {
        if (error)
            *error = QStringLiteral("No model is loaded.");
        return QImage();
    }
    try {
        std::vector<float> tensor = toTensor(image);
        const std::array<int64_t, 4> shape{1, 3, kSide, kSide};
        const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        Ort::Value input = Ort::Value::CreateTensor<float>(memory, tensor.data(), tensor.size(),
                                                           shape.data(), shape.size());

        Ort::AllocatorWithDefaultOptions allocator;
        const Ort::AllocatedStringPtr inputName = d->session->GetInputNameAllocated(0, allocator);
        const Ort::AllocatedStringPtr outputName = d->session->GetOutputNameAllocated(0, allocator);
        const char *inputs[] = {inputName.get()};
        const char *outputs[] = {outputName.get()};

        std::vector<Ort::Value> results =
            d->session->Run(Ort::RunOptions{nullptr}, inputs, &input, 1, outputs, 1);
        const std::vector<int64_t> size = results.front().GetTensorTypeAndShapeInfo().GetShape();
        if (size.size() < 2)
            throw Ort::Exception("The model returned no mask.", ORT_FAIL);
        return toMask(results.front().GetTensorData<float>(), int(size[size.size() - 1]),
                      int(size[size.size() - 2]), image.size());
    } catch (const Ort::Exception &problem) {
        if (error)
            *error = QString::fromUtf8(problem.what());
        return QImage();
    }
}

}  // namespace matte
