/*
 * wordcount.cc — count words per spine item of a plain epub.
 *
 * Reads the epub zip with Qt's private QZipReader (exported by libQt6Gui on
 * firmware 5.18+), locates the OPF, parses the spine order, and counts words in
 * each spine XHTML file. Self-contained: no libnickel internals are touched
 * here — only Qt6Core/Qt6Gui.
 */

#include "wordcount.h"
#include "wordcount_logic.h"

#include <QByteArray>
#include <QHash>
#include <QXmlStreamReader>
#include <QtGui/private/qzipreader_p.h>

namespace {

// A QZipReader that refuses entries larger than a size limit.
class BoundedZip
{
public:
    BoundedZip(const QString &path, qint64 max_entry_bytes)
        : zip_(path), max_(max_entry_bytes)
    {
        if (!zip_.isReadable()) {
            return;
        }
        const auto infos = zip_.fileInfoList();
        for (const QZipReader::FileInfo &fi : infos) {
            if (!fi.isDir) {
                sizes_.insert(fi.filePath, fi.size);
            }
        }
    }

    bool isReadable() const { return zip_.isReadable(); }
    const QHash<QString, qint64> &entries() const { return sizes_; }

    // Empty if the entry is missing or over the limit (checked against the
    // header's uncompressed size, before anything is inflated).
    QByteArray read(const QString &path)
    {
        const auto it = sizes_.constFind(path);
        if (it == sizes_.constEnd() || *it > max_) {
            return QByteArray();
        }
        return zip_.fileData(path);
    }

private:
    QZipReader zip_;
    qint64 max_;
    QHash<QString, qint64> sizes_;
};

// Locate the OPF path: META-INF/container.xml first, then *.opf fallback.
QString find_opf(BoundedZip &zip)
{
    const QByteArray container = zip.read(QStringLiteral("META-INF/container.xml"));
    if (!container.isEmpty()) {
        QXmlStreamReader xml(container);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QLatin1String("rootfile")) {
                const QString full = xml.attributes().value(QLatin1String("full-path")).toString();
                if (!full.isEmpty()) {
                    return full;
                }
            }
        }
    }
    for (auto it = zip.entries().constBegin(); it != zip.entries().constEnd(); ++it) {
        if (it.key().endsWith(QLatin1String(".opf"), Qt::CaseInsensitive)) {
            return it.key();
        }
    }
    return QString();
}

} // namespace

QList<int> count_epub_words(const QString &epub_path, qint64 max_entry_bytes)
{
    QList<int> counts;
    BoundedZip zip(epub_path, max_entry_bytes);
    if (!zip.isReadable()) {
        return counts;
    }

    const QString opf_path = find_opf(zip);
    if (opf_path.isEmpty()) {
        return counts;
    }

    const QByteArray opf = zip.read(opf_path);
    if (opf.isEmpty()) {
        return counts;
    }

    const QStringList spine_hrefs = parse_spine(opf);
    if (spine_hrefs.isEmpty()) {
        return counts;
    }

    // OPF directory (for resolving relative manifest hrefs).
    const int slash = opf_path.lastIndexOf(QLatin1Char('/'));
    const QString opf_dir = slash >= 0 ? opf_path.left(slash) : QString();

    // Exactly one entry per spine item, in spine order: Nickel indexes the list
    // by spine position (chapter bars, mergedTOCAndSpineIndex fallback), so a
    // missing, empty or oversized file must count as 0 rather than shift the
    // rest.
    for (const QString &href : spine_hrefs) {
        const QByteArray xhtml = zip.read(join_path(opf_dir, href));
        counts.append(xhtml.isEmpty() ? 0 : count_words(QString::fromUtf8(xhtml)));
    }
    // Nothing readable at all: report "no data" instead of an all-zero list.
    for (const int c : counts) {
        if (c > 0) {
            return counts;
        }
    }
    return QList<int>();
}
