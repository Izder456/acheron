#include "ImageUpload.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QLocale>
#include <QMimeDatabase>

namespace Acheron {
namespace UI {
namespace ImageUpload {

namespace {

QString tr(const char *text)
{
    return QCoreApplication::translate("ImageUpload", text);
}

bool readFile(const QString &path, QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    bytes = file.readAll();
    return true;
}

bool isAnimation(const QByteArray &bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    QImageReader reader(&buffer);
    return reader.supportsAnimation() && reader.imageCount() > 1;
}

} // namespace

QString fileFilter()
{
    return tr("Images (*.jpg *.jpeg *.jfif *.png *.gif *.webp *.avif)");
}

QString dataUri(const QByteArray &bytes, const QString &mimeType)
{
    return QStringLiteral("data:%1;base64,%2").arg(mimeType, QString::fromLatin1(bytes.toBase64()));
}

QByteArray md5Hex(const QByteArray &bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Md5).toHex();
}

PreparedImage prepareCropped(const QString &path, int maxWidth, int maxHeight)
{
    PreparedImage prepared;
    QByteArray bytes;
    if (!readFile(path, bytes)) {
        prepared.error = tr("Unable to process image");
        return prepared;
    }
    prepared.originalMd5 = md5Hex(bytes);

    const QString mimeType = QMimeDatabase().mimeTypeForData(bytes).name();
    if (isAnimation(bytes)) {
        prepared.animated = true;
        prepared.dataUri = dataUri(bytes, mimeType);
        return prepared;
    }

    QImage image;
    if (!image.loadFromData(bytes)) {
        if (mimeType == QLatin1String("image/avif"))
            prepared.dataUri = dataUri(bytes, mimeType);
        else
            prepared.error = tr("Unable to process image");
        return prepared;
    }

    const double targetAspect = double(maxWidth) / maxHeight;
    QRect crop = image.rect();
    if (double(image.width()) / image.height() > targetAspect)
        crop.setWidth(qRound(image.height() * targetAspect));
    else
        crop.setHeight(qRound(image.width() / targetAspect));
    crop.moveCenter(image.rect().center());

    QImage cropped = image.copy(crop);
    if (cropped.width() > maxWidth || cropped.height() > maxHeight)
        cropped = cropped.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    prepared.dataUri = dataUri(encodePng(cropped), QStringLiteral("image/png"));
    return prepared;
}

QByteArray encodePng(const QImage &image)
{
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png;
}

PreparedImage prepareRaw(const QString &path, qint64 maxBytes)
{
    PreparedImage prepared;
    if (QFileInfo(path).size() > maxBytes) {
        prepared.error = tr("Max file size is %1 please.").arg(QLocale().formattedDataSize(maxBytes));
        return prepared;
    }
    QByteArray bytes;
    if (!readFile(path, bytes)) {
        prepared.error = tr("Unable to process image");
        return prepared;
    }

    prepared.originalMd5 = md5Hex(bytes);
    prepared.animated = isAnimation(bytes);
    prepared.dataUri = dataUri(bytes, QMimeDatabase().mimeTypeForData(bytes).name());
    return prepared;
}

} // namespace ImageUpload
} // namespace UI
} // namespace Acheron
