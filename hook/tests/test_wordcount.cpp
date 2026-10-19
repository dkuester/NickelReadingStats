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
    // only the <body> counts (W1)
    CHECK_EQ(count_words(QString("<html><head><title>Ignored Title</title></head>"
                                 "<body class=\"c\"><p>one two</p></body></html>")), 2);
    CHECK_EQ(count_words(QString("<head><title>x y</title></head>a b")), 2);
    CHECK_EQ(count_words(QString("<BODY>a b c</BODY>")), 3);
    // self-closing <script/> must not swallow prose; comments are dropped (W2)
    CHECK_EQ(count_words(QString("a<script src=\"x.js\"/>b c<script>d</script>e")), 4);
    CHECK_EQ(count_words(QString("a<!-- <p> x > y --> b")), 2);
    CHECK_EQ(count_words(QString("<styles>a</styles>")), 1);
    // punctuation-only tokens are not words (W3)
    CHECK_EQ(count_words(QString::fromUtf8("Chapter * * * end \xe2\x80\x94 fin")), 3);
    CHECK_EQ(count_words(QString("x &mdash; y")), 2);
    CHECK_EQ(count_words(QString("caf&eacute; ok")), 2);
    // numeric and named space entities separate words (W4)
    CHECK_EQ(count_words(QString("a&#xa0;b")), 2);
    CHECK_EQ(count_words(QString("a&#xA0;b")), 2);
    CHECK_EQ(count_words(QString("a&#8201;b")), 2);
    CHECK_EQ(count_words(QString("a&thinsp;b&ensp;c&emsp;d")), 4);
    CHECK_EQ(count_words(QString("a&#9999999;b")), 2); // > U+10FFFF -> space
    CHECK_EQ(count_words(QString("a&#0;b")), 2);

    // --- join_path: manifest href resolution ---
    CHECK_EQ(join_path(QString("OEBPS"), QString("text/ch1.xhtml")), QString("OEBPS/text/ch1.xhtml"));
    CHECK_EQ(join_path(QString("OEBPS"), QString("/text/ch1.xhtml")), QString("text/ch1.xhtml"));
    CHECK_EQ(join_path(QString(), QString("ch1.xhtml")), QString("ch1.xhtml"));
    CHECK_EQ(join_path(QString("OEBPS"), QString("./ch1.xhtml")), QString("OEBPS/ch1.xhtml"));
    CHECK_EQ(join_path(QString("OEBPS/text"), QString("../images/x.jpg")), QString("OEBPS/images/x.jpg"));
    CHECK_EQ(join_path(QString("OEBPS"), QString("../toc.xhtml")), QString("toc.xhtml"));
    CHECK_EQ(join_path(QString("OEBPS"), QString("a/../b.xhtml")), QString("OEBPS/b.xhtml"));
    // hrefs are URLs: percent-encoded names resolve to the literal zip entry
    CHECK_EQ(join_path(QString("OEBPS"), QString("Kapitel%201.xhtml")), QString("OEBPS/Kapitel 1.xhtml"));
    CHECK_EQ(join_path(QString("OEBPS"), QString("%C3%BCber.xhtml")), QString::fromUtf8("OEBPS/\xc3\xbc" "ber.xhtml"));

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
