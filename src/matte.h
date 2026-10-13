#pragma once

#include <QByteArray>
#include <QImage>
#include <QRectF>
#include <QString>
#include <QUrl>

#include <memory>
#include <vector>

// Cutting a person out of any background with a neural network (BiRefNet,
// run by ONNX Runtime). Slower than keying a green screen and needs a model
// file, which is downloaded the first time it is wanted.
namespace matte {

struct Model {
    QString id;
    QString label;
    QString file;
    QUrl url;
    QByteArray sha256;  // hex
    qint64 bytes = 0;
};

// The smaller network: about a fifth of the download and half the wait.
const Model &fast();
// The full network, for the hair and bat edges the smaller one fumbles.
const Model &best();
// Null for anything that is not a model's id.
const Model *byId(const QString &id);

// Where models are kept: ~/.local/share/omalift/models, unless
// OMALIFT_MODEL_DIR says otherwise.
QString modelDir();
QString modelPath(const Model &model);
bool installed(const Model &model);
// "214 MB"
QString sizeLabel(const Model &model);

// The networks take a square picture of this many pixels a side.
constexpr int kSide = 1024;

// The photo as the network wants it: squashed to kSide square, as three
// planes of red, green and blue, each normalised.
std::vector<float> toTensor(const QImage &image);

// The network's answer as a greyscale mask the size of the photo, white
// where the person is. `scores` is one plane of width x height raw scores.
QImage toMask(const float *scores, int width, int height, const QSize &size);

// The photo with the mask as its transparency, as non-premultiplied ARGB32.
QImage withMask(const QImage &image, const QImage &mask);

// Masked, cropped to `crop` (fractions of the photo) and trimmed to the
// person. Null when nothing is left.
QImage cutout(const QImage &image, const QImage &mask, const QRectF &crop = QRectF(0, 0, 1, 1));

// A loaded model. Opening one takes a few seconds and a lot of memory, so one
// is kept for as long as the same model is being used.
class Session {
public:
    Session();
    ~Session();

    bool open(const QString &path, QString *error = nullptr);
    // The mask for a photo, or a null image and an error.
    QImage mask(const QImage &image, QString *error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

}  // namespace matte
