#pragma once

#include <QImage>
#include <QSize>
#include <QString>

namespace kg {

/**
 * HEIC/HEIF decode fallback.
 *
 * Qt can only read HEIC/HEIF when a plugin (qt6-imageformats / kimageformats)
 * backed by libheif is installed. When it is not, the file still indexes - the
 * extension and MIME are known - but every thumbnail and the viewer would show a
 * placeholder. `heif-convert` (libheif-examples) is a small, optional runtime
 * dependency that closes that gap without linking libheif into the app.
 *
 * Nothing here is required for a working build: with no `heif-convert` on PATH
 * the functions are cheap no-ops and the existing placeholder behaviour stands.
 */
namespace ImageLoader {

/// True when `path` looks like a HEIC/HEIF file (by extension).
bool isHeifPath(const QString &path);

/// True when `heif-convert` was found on PATH.
bool heifConvertAvailable();

/**
 * Decodes a HEIC/HEIF file by first converting it to a temporary JPEG with
 * `heif-convert`. When `targetEdge` is positive the result is downscaled to it,
 * matching what QImageReader would have done for a natively supported format.
 *
 * Returns a null QImage when the tool is missing, the conversion fails, or the
 * output cannot be read.
 */
QImage decodeHeif(const QString &path, int targetEdge = 0, int allocationLimit = 1024);

} // namespace ImageLoader

} // namespace kg
