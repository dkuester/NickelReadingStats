// Host test: run the REAL count_epub_words() (wordcount.cc, incl. QZipReader)
// against a fixture EPUB and assert the exact per-chapter word counts. This
// covers the ZIP/OPF/spine path that the pure-logic test cannot: locating the
// OPF via container.xml, resolving spine hrefs, and reading files out of the
// zip. Unlike test_wordcount.cpp, it needs QtGui's PRIVATE QZipReader header,
// so it only runs where those headers are available.
#include "../src/wordcount.h"

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

    const QList<int> counts = count_epub_words(fixture);
    check("spine item count", counts.size(), 3);
    if (counts.size() == 3) {
        check("ch1 words", counts[0], 4);   // alpha beta gamma delta
        check("ch2 words", counts[1], 2);   // epsilon zeta
        check("ch3 words", counts[2], 3);   // eta theta iota
    }

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
