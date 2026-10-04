#pragma once

#include <QByteArray>
#include <QString>

class QImage;

namespace Acheron {
namespace UI {
namespace ImageUpload {

struct PreparedImage
{
    QString dataUri;
    QByteArray originalMd5;
    QString error;
    bool animated = false;
};

QString fileFilter();

PreparedImage prepareCropped(const QString &path, int maxWidth, int maxHeight);
PreparedImage prepareRaw(const QString &path, qint64 maxBytes);

QString dataUri(const QByteArray &bytes, const QString &mimeType);
QByteArray md5Hex(const QByteArray &bytes);
QByteArray encodePng(const QImage &image);

} // namespace ImageUpload
} // namespace UI
} // namespace Acheron
