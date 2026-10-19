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
#include <QUrl>
#include <QXmlStreamReader>

// Count the words in an XHTML string: whitespace-separated tokens in the
// <body> that contain at least one letter or digit.
inline int count_words(const QString &html)
{
    QString text = html;

    // Comments first: they may contain '>' (or tags) that would otherwise
    // break the tag stripping below.
    static const QRegularExpression comment(
        "<!--.*?-->", QRegularExpression::DotMatchesEverythingOption);
    text.replace(comment, QLatin1String(" "));

    // Only the <body> is prose; <head> holds the <title>, which would add a few
    // words to every chapter. Without a <body>, at least drop the <head>.
    const int body_open = text.indexOf(QLatin1String("<body"), 0, Qt::CaseInsensitive);
    if (body_open >= 0) {
        const int body_start = text.indexOf(QLatin1Char('>'), body_open);
        const int body_end = text.lastIndexOf(QLatin1String("</body"), -1, Qt::CaseInsensitive);
        if (body_start < 0) {
            text.clear();
        } else {
            text = text.mid(body_start + 1, body_end > body_start ? body_end - body_start - 1 : -1);
        }
    } else {
        static const QRegularExpression head(
            "<head\\b.*?</head\\s*>",
            QRegularExpression::CaseInsensitiveOption |
                QRegularExpression::DotMatchesEverythingOption);
        text.replace(head, QLatin1String(" "));
    }

    // Drop <script>/<style> blocks including their contents. A self-closing
    // <script src="..."/> has no contents and must not open a block (it would
    // swallow all prose up to the next </script>); the tag pass removes it.
    // Replace with a space (not empty) so the words on either side of the
    // block do not get concatenated.
    static const QRegularExpression block(
        "<(script|style)\\b[^>]*(?<!/)>.*?</\\1\\s*>",
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);
    text.replace(block, QLatin1String(" "));

    // Replace every remaining tag with a space for the same reason: without
    // it, "one</p><p>two" would collapse to "onetwo" (one word instead of two).
    static const QRegularExpression tag("<[^>]*>");
    text.replace(tag, QLatin1String(" "));

    // Numeric character references: decode, so &#160; / &#x2009; become real
    // (non-breaking, thin) spaces that separate words.
    static const QRegularExpression numeric("&#([xX][0-9a-fA-F]{1,6}|[0-9]{1,7});");
    QString decoded;
    decoded.reserve(text.size());
    int last = 0;
    for (auto it = numeric.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        decoded += QStringView(text).mid(last, m.capturedStart() - last);
        const QString num = m.captured(1);
        bool ok = false;
        const uint code = num.startsWith(QLatin1Char('x'), Qt::CaseInsensitive)
                              ? num.mid(1).toUInt(&ok, 16)
                              : num.toUInt(&ok, 10);
        if (ok && code > 0 && code <= 0x10FFFF) {
            const char32_t cp = code;
            decoded += QString::fromUcs4(&cp, 1);
        } else {
            decoded += QLatin1Char(' ');
        }
        last = m.capturedEnd();
    }
    decoded += QStringView(text).mid(last);
    text = decoded;

    // Named entities that separate words.
    for (const char *space : {"&nbsp;", "&ensp;", "&emsp;", "&thinsp;"}) {
        text.replace(QLatin1String(space), QLatin1String(" "));
    }
    // Any other named entity (&amp;, &mdash;, &eacute;, ...) becomes a
    // symbol: it glues to adjacent text like the real character would, and on
    // its own it is punctuation, not a word.
    static const QRegularExpression named("&[A-Za-z][A-Za-z0-9]*;");
    text.replace(named, QStringLiteral("·"));

    // A token counts as a word only if it contains a letter or digit, so
    // scene breaks ("* * *") and dashes are not counted.
    int count = 0;
    bool in_token = false;
    bool has_alnum = false;
    for (const QChar &c : text) {
        if (c.isSpace()) {
            if (in_token && has_alnum) {
                ++count;
            }
            in_token = false;
            has_alnum = false;
        } else {
            in_token = true;
            has_alnum = has_alnum || c.isLetterOrNumber();
        }
    }
    if (in_token && has_alnum) {
        ++count;
    }
    return count;
}

// Resolve a manifest href (relative to the OPF directory) to a zip path.
// Manifest hrefs are URLs, so "Chapter%201.xhtml" names the zip entry
// "Chapter 1.xhtml": percent-decode before joining.
inline QString join_path(const QString &opf_dir, const QString &raw_href)
{
    const QString href = QUrl::fromPercentEncoding(raw_href.toUtf8());
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
