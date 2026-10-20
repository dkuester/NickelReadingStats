// Host test for the background word counting in readingstats.cc: cache,
// in-flight waiters, and delivery only to views that still show the same
// book. readingstats.cc is included directly so the test can replace the
// libnickel function pointers (resolved via dlsym on the device) with fakes.
// The fake "ReadingView" is a plain QObject: like the real one (QWidget at
// offset 0), its pointer is its QObject.
#include "../src/readingstats.cc"
#include "nh_log_stub.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QHash>
#include <QThread>
#include <cstdio>

static int failures = 0;

static void check(const char *what, bool ok)
{
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) {
        ++failures;
    }
}

// --- fakes for the libnickel side -------------------------------------------

struct FakeBook {
    QByteArray mime;
    QString id;
};
static QHash<const void *, FakeBook> g_books;           // content ptr -> book
static QHash<ReadingView *, QList<QList<int>>> g_got;   // view -> deliveries

static const void *content_of(ReadingView *view)
{
    return reading_view_content(view);
}

static QByteArray &fake_getMimeType(const Content *content)
{
    return g_books[content].mime;
}

static QString fake_getId(const Content *content, const QString &)
{
    return g_books.value(content).id;
}

static void fake_gotWordCounts(ReadingView *view, const QList<int> &counts)
{
    g_got[view].append(counts);
}

static bool fake_isFixedLayout(ReadingView *)
{
    return false;
}

static void open_book(QObject *view, const QString &path)
{
    g_books.insert(content_of(view), FakeBook{"application/epub+zip", "file://" + path});
}

// Process events until `done` or a timeout.
template <typename Pred>
static bool wait_for(Pred done)
{
    QDeadlineTimer deadline(5000);
    while (!done() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return done();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const QString fixture = QFileInfo(argc > 1 ? QString::fromLocal8Bit(argv[1])
                                               : QStringLiteral("fixtures/sample.epub"))
                                .absoluteFilePath();

    Content__getMimeType = fake_getMimeType;
    Content__getId = fake_getId;
    ReadingView__gotWordCounts = fake_gotWordCounts;
    ReadingView__isFixedLayout = fake_isFixedLayout;
    ReadingView__updateWordCounts = nullptr;  // no original to chain to

    // 1. Counting runs in the background and is delivered to the open view.
    QObject a;
    open_book(&a, fixture);
    _nh_ReadingView__updateWordCounts(&a);
    check("not delivered synchronously (background)", g_got.value(&a).isEmpty());
    check("delivered after the worker finished", wait_for([&] { return g_got.value(&a).size() == 1; }));
    check("delivered counts are the real ones",
          g_got.value(&a).value(0) == (QList<int>{4, 2, 0, 3, 2, 600}));

    // 2. Second open of the same, unchanged file: cache hit, synchronous.
    QObject b;
    open_book(&b, fixture);
    _nh_ReadingView__updateWordCounts(&b);
    check("cache hit delivers synchronously", g_got.value(&b).size() == 1);

    // 3. A view closed while counting gets nothing (and nothing crashes).
    g_word_count_cache.clear();
    auto *closed = new QObject;
    ReadingView *closed_ptr = closed;
    open_book(closed, fixture);
    _nh_ReadingView__updateWordCounts(closed);
    delete closed;
    check("in-flight entry finishes", wait_for([&] { return g_word_count_waiters.isEmpty(); }));
    check("closed view got no delivery", !g_got.contains(closed_ptr));

    // 4. A view reused for another book meanwhile gets nothing.
    g_word_count_cache.clear();
    QObject c;
    open_book(&c, fixture);
    _nh_ReadingView__updateWordCounts(&c);
    g_books[content_of(&c)].id = QStringLiteral("file:///mnt/onboard/other.epub");
    check("in-flight entry finishes (reused view)", wait_for([&] { return g_word_count_waiters.isEmpty(); }));
    check("reused view got no delivery", g_got.value(&c).isEmpty());

    // 5. Two views open the same book while it is counted: one count, both served.
    g_word_count_cache.clear();
    QObject d, e;
    open_book(&d, fixture);
    open_book(&e, fixture);
    _nh_ReadingView__updateWordCounts(&d);
    _nh_ReadingView__updateWordCounts(&e);
    check("second request joins the in-flight count",
          g_word_count_waiters.value(fixture).size() == 2);
    check("both views served", wait_for([&] {
              return g_got.value(&d).size() == 1 && g_got.value(&e).size() == 1;
          }));

    // 6. A kepub is left to Nickel.
    QObject k;
    g_books.insert(content_of(&k), FakeBook{"application/x-kobo-epub+zip", "file://" + fixture});
    _nh_ReadingView__updateWordCounts(&k);
    check("kepub: no count started", g_word_count_waiters.isEmpty());

    if (failures) {
        std::printf("%d hook check(s) failed\n", failures);
        return 1;
    }
    std::printf("all hook checks passed\n");
    return 0;
}
