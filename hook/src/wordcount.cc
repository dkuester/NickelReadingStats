/*
 * wordcount.cc — count words per spine item of a plain epub.
 *
 * Reads the epub zip with Qt's private QZipReader (present in the 5.18
 * libQt6Gui), locates the OPF, parses the spine order, and counts words in
 * each spine XHTML file. Self-contained: no libnickel internals are touched
 * here — only Qt6Core/Qt6Gui.
 */

#include "wordcount.h"
#include "wordcount_logic.h"

#include <QByteArray>
#include <QXmlStreamReader>
#include <QtGui/private/qzipreader_p.h>

// Locate the OPF path: META-INF/container.xml first, then *.opf fallback.
static QString find_opf(QZipReader &zip)
{
    const QByteArray container = zip.fileData(QStringLiteral("META-INF/container.xml"));
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
    const auto infos = zip.fileInfoList();
    for (const QZipReader::FileInfo &fi : infos) {
        if (fi.filePath.endsWith(QLatin1String(".opf"), Qt::CaseInsensitive)) {
            return fi.filePath;
        }
    }
    return QString();
}

QList<int> count_epub_words(const QString &epub_path)
{
    QList<int> counts;
    QZipReader zip(epub_path);
    if (!zip.isReadable()) {
        return counts;
    }

    const QString opf_path = find_opf(zip);
    if (opf_path.isEmpty()) {
        return counts;
    }

    const QByteArray opf = zip.fileData(opf_path);
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
    // missing or empty file must count as 0 rather than shift the rest.
    for (const QString &href : spine_hrefs) {
        const QByteArray xhtml = zip.fileData(join_path(opf_dir, href));
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
