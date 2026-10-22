// Host test: run the REAL count_epub_words() (wordcount.cc, incl. QZipReader)
// against a fixture EPUB and assert the exact per-chapter word counts. This
// covers the ZIP/OPF/spine path that the pure-logic test cannot: locating the
// OPF via container.xml, resolving spine hrefs, and reading files out of the
// zip. Unlike test_wordcount.cpp, it needs QtGui's PRIVATE QZipReader header,
// so it only runs where those headers are available.
#include "../src/wordcount.h"

#include <QDir>
#include <QFileInfo>
#include <QList>
#include <QString>
#include <cstdio>

static int failures = 0;

static void check(const char *what, int got, int want)
{
    if (got == want) {
        std::printf("ok   %-28s %d\n", what, got);
    } else {
        std::printf("FAIL %-28s got %d want %d\n", what, got, want);
        ++failures;
    }
}

int main(int argc, char **argv)
{
    const QString fixture = argc > 1 ? QString::fromLocal8Bit(argv[1])
                                     : QStringLiteral("fixtures/sample.epub");

    // One entry per spine item; see fixtures/make_sample_epub.py.
    const QList<int> counts = count_epub_words(fixture);
    check("spine item count", counts.size(), 6);
    if (counts.size() == 6) {
        check("ch1 words", counts[0], 4);           // alpha beta gamma delta
        check("ch2 words", counts[1], 2);           // epsilon zeta
        check("missing file -> 0", counts[2], 0);   // keeps later indices aligned
        check("ch3 words", counts[3], 3);           // eta theta iota
        check("subdir + %-encoded href", counts[4], 2); // Text/Kapitel%204.xhtml
        check("big chapter", counts[5], 600);
    }

    // Per-entry size limit: big.xhtml (~3 KB) is over 2 KB and counts as 0,
    // the small chapters and the OPF still fit.
    const QList<int> limited = count_epub_words(fixture, 2048);
    check("limited: item count", limited.size(), 6);
    if (limited.size() == 6) {
        check("limited: ch1 words", limited[0], 4);
        check("limited: big chapter -> 0", limited[5], 0);
    }
    // A limit below the OPF size yields no result at all.
    check("limit < OPF -> empty", count_epub_words(fixture, 64).size(), 0);

    // A DRM epub (encryption.xml lists a spine item) yields no result.
    const QString encrypted = QFileInfo(fixture).dir().filePath(QStringLiteral("encrypted.epub"));
    check("encrypted chapter -> empty", count_epub_words(encrypted).size(), 0);

    const QList<int> missing =
        count_epub_words(QStringLiteral("fixtures/does-not-exist.epub"));
    check("missing file -> empty", missing.size(), 0);

    if (failures) {
        std::printf("%d epub check(s) failed\n", failures);
        return 1;
    }
    std::printf("all epub checks passed\n");
    return 0;
}
