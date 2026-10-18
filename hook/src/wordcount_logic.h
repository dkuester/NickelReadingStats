/*
 * wordcount_logic.h — pure word-counting logic for NickelReadingStats
 *
 * The Qt-version-independent parts of the epub word counter: counting words
 * in an XHTML string, resolving manifest hrefs to zip paths, and parsing the
 * OPF spine. Extracted from wordcount.cc so they can be unit-tested on the
 * host against Qt5Core/Qt6Core (QZipReader, the QtGui private API, is NOT
 * needed here). Keep this file free of any QtGui/libnickel dependency.
 */
#pragma once

#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QXmlStreamReader>

// Count whitespace-separated non-empty tokens in an XHTML string.
inline int count_words(const QString &html)
{
    QString text = html;

    // Drop <script>/<style> blocks including their contents (case-insensitive,
    // non-greedy). These are not prose. Replace with a space (not empty) so two
    // words on either side of the block do not get concatenated.
    static const QRegularExpression block(
        "<(script|style)[^>]*>.*?</\\1>",
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);
    text.replace(block, QLatin1String(" "));

    // Replace every remaining tag with a space for the same reason: without
    // it, "one</p><p>two" would collapse to "onetwo" (one word instead of two).
    static const QRegularExpression tag("<[^>]*>");
    text.replace(tag, QLatin1String(" "));

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
inline QString join_path(const QString &opf_dir, const QString &href)
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

// Parse the OPF and return spine hrefs in reading order.
inline QStringList parse_spine(const QByteArray &opf)
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
