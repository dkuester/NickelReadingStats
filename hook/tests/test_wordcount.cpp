/*
 * test_wordcount.cpp — host unit tests for the pure word-counting logic.
 *
 * Exercises count_words(), join_path() and parse_spine() against the HOST Qt
 * (Qt5Core or Qt6Core). Deliberately does NOT touch QZipReader (QtGui private
 * API) or the cross toolchain, so it runs anywhere with a Qt dev install.
 */
#include "../src/wordcount_logic.h"

#include <QByteArray>
#include <cstdio>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        ++g_checks;                                                        \
        if (!(cond)) {                                                     \
            ++g_failures;                                                  \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,   \
                         #cond);                                           \
        }                                                                  \
    } while (0)

#define CHECK_EQ(a, b) CHECK((a) == (b))

int main()
{
    // --- count_words: tokenising, tag/script stripping, entity decoding ---
    CHECK_EQ(count_words(QString()), 0);
    CHECK_EQ(count_words(QString("hello world")), 2);
    CHECK_EQ(count_words(QString("<p>hello <b>world</b></p>")), 2);
    CHECK_EQ(count_words(QString("a   b\n\tc")), 3);
    // script/style blocks (and their contents) are not prose
    CHECK_EQ(count_words(QString("one<script>var x = 1; two three</script>four")), 2);
    CHECK_EQ(count_words(QString("one<style>p { margin: 0; }</style>two")), 2);
    CHECK_EQ(count_words(QString("<SCRIPT>alert(1)</SCRIPT>foo")), 1); // case-insensitive
    // entities that separate words vs. glue characters
    CHECK_EQ(count_words(QString("foo&nbsp;bar")), 2);
    CHECK_EQ(count_words(QString("foo&#160;bar")), 2);
    CHECK_EQ(count_words(QString("don&#39;t")), 1);
    CHECK_EQ(count_words(QString("foo&amp;bar")), 1);
    CHECK_EQ(count_words(QString("a&rsquo;b")), 1);
    CHECK_EQ(count_words(QString("<div class=\"x\">\n<p>para one</p>\n<p>para two</p>\n</div>")), 4);

    // --- join_path: manifest href resolution ---
    CHECK_EQ(join_path(QString("OEBPS"), QString("text/ch1.xhtml")), QString("OEBPS/text/ch1.xhtml"));
    CHECK_EQ(join_path(QString("OEBPS"), QString("/text/ch1.xhtml")), QString("text/ch1.xhtml"));
    CHECK_EQ(join_path(QString(), QString("ch1.xhtml")), QString("ch1.xhtml"));
    CHECK_EQ(join_path(QString("OEBPS"), QString("./ch1.xhtml")), QString("OEBPS/ch1.xhtml"));
    CHECK_EQ(join_path(QString("OEBPS/text"), QString("../images/x.jpg")), QString("OEBPS/images/x.jpg"));
    CHECK_EQ(join_path(QString("OEBPS"), QString("../toc.xhtml")), QString("toc.xhtml"));
    CHECK_EQ(join_path(QString("OEBPS"), QString("a/../b.xhtml")), QString("OEBPS/b.xhtml"));

    // --- parse_spine: manifest + spine order ---
    {
        const QByteArray opf =
            "<?xml version=\"1.0\"?>"
            "<package>"
            "<manifest>"
            "<item id=\"c1\" href=\"Text/ch1.xhtml\"/>"
            "<item id=\"c2\" href=\"Text/ch2.xhtml\"/>"
            "<item id=\"c3\" href=\"Text/ch3.xhtml\"/>"
            "</manifest>"
            "<spine>"
            "<itemref idref=\"c2\"/>"
            "<itemref idref=\"c1\"/>"
            "<itemref idref=\"c3\"/>"
            "</spine>"
            "</package>";
        const QStringList spine = parse_spine(opf);
        CHECK_EQ(spine.size(), 3);
        CHECK_EQ(spine[0], QString("Text/ch2.xhtml"));
        CHECK_EQ(spine[1], QString("Text/ch1.xhtml"));
        CHECK_EQ(spine[2], QString("Text/ch3.xhtml"));
    }
    {
        // an idref pointing at an unknown manifest id is skipped, not fatal
        const QByteArray opf =
            "<package><manifest><item id=\"a\" href=\"a.xhtml\"/></manifest>"
            "<spine><itemref idref=\"missing\"/><itemref idref=\"a\"/></spine></package>";
        const QStringList spine = parse_spine(opf);
        CHECK_EQ(spine.size(), 1);
        CHECK_EQ(spine[0], QString("a.xhtml"));
    }
    {
        CHECK_EQ(parse_spine(QByteArray()).size(), 0);
    }

    std::printf("%d checks, %d failure(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
