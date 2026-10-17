/*
 * wordcount.cc — count words per spine item of a plain epub.
 *
 * Reads the epub zip with Qt's private QZipReader (present in the 5.18
 * libQt6Gui), locates the OPF, parses the spine order, and counts words in
 * each spine XHTML file. Self-contained: no libnickel internals are touched
 * here — only Qt6Core/Qt6Gui.
 */

#include "wordcount.h"

#include <QByteArray>
#include <QHash>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <QtGui/private/qzipreader_p.h>

// Count whitespace-separated non-empty tokens in an XHTML string.
static int count_words(const QString &html)
{
    QString text = html;

    // Drop <script>/<style> blocks including their contents (case-insensitive,
    // non-greedy). These are not prose.
    static const QRegularExpression block(
        "<(script|style)[^>]*>.*?</\\1>",
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);
    text.remove(block);

    // Drop every remaining tag.
    static const QRegularExpression tag("<[^>]*>");
    text.remove(tag);

    // Decode the entities that commonly separate real words.
    text.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    text.replace(QLatin1String("&#160;"), QLatin1String(" "));
    text.replace(QLatin1String("&amp;"), QLatin1String("&"));
    text.replace(QLatin1String("&lt;"), QLatin1String("<"));
    text.replace(QLatin1String("&gt;"), QLatin1String(">"));
    text.replace(QLatin1String("&quot;"), QLatin1String("\""));
    text.replace(QLatin1String("&#39;"), QLatin1String("'"));
    text.replace(QLatin1String("&rsquo;"), QLatin1String("'"));
    text.replace(QLatin1String("&ldquo;"), QLatin1String("\""));
    text.replace(QLatin1String("&rdquo;"), QLatin1String("\""));

    int count = 0;
    bool in_word = false;
    for (const QChar &c : text) {
        if (c.isSpace()) {
            in_word = false;
        } else if (!in_word) {
            in_word = true;
            ++count;
        }
    }
    return count;
}

// Resolve a manifest href (relative to the OPF directory) to a zip path.
static QString join_path(const QString &opf_dir, const QString &href)
{
    if (href.startsWith(QLatin1Char('/'))) {
        return href.mid(1);
    }
    if (opf_dir.isEmpty()) {
        return href;
    }
    QString joined = opf_dir + QLatin1Char('/') + href;
    // Collapse a/./b and a/../b without touching the leading component.
    QStringList parts = joined.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QStringList out;
    for (const QString &p : parts) {
        if (p == QLatin1String(".")) {
            continue;
        }
        if (p == QLatin1String("..")) {
            if (!out.isEmpty()) {
                out.removeLast();
            }
            continue;
        }
        out.append(p);
    }
    return out.join(QLatin1Char('/'));
}

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

// Parse the OPF and return spine hrefs in reading order.
static QStringList parse_spine(const QByteArray &opf)
{
    QHash<QString, QString> manifest; // id -> href
    QStringList idrefs;               // spine order
    QXmlStreamReader xml(opf);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) {
            continue;
        }
        if (xml.name() == QLatin1String("item")) {
            const QString id = xml.attributes().value(QLatin1String("id")).toString();
            const QString href = xml.attributes().value(QLatin1String("href")).toString();
            manifest.insert(id, href);
        } else if (xml.name() == QLatin1String("itemref")) {
            idrefs.append(xml.attributes().value(QLatin1String("idref")).toString());
        }
    }

    QStringList spine;
    for (const QString &idref : idrefs) {
        const auto it = manifest.constFind(idref);
        if (it != manifest.constEnd()) {
            spine.append(*it);
        }
    }
    return spine;
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

    for (const QString &href : spine_hrefs) {
        const QString full = join_path(opf_dir, href);
        const QByteArray xhtml = zip.fileData(full);
        if (xhtml.isEmpty()) {
            continue;
        }
        counts.append(count_words(QString::fromUtf8(xhtml)));
    }
    return counts;
}
