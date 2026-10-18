# Host unit tests for the word-counting code.
#   test_wordcount  — pure logic (wordcount_logic.h), QtCore only, portable.
#   test_epub_words — the REAL count_epub_words() incl. QZipReader against a
#                     fixture EPUB; needs QtGui's PRIVATE qzipreader_p.h, so it
#                     only builds where those private headers are installed.
# Both run against the HOST Qt (5 or 6) — no cross toolchain.

CXX      ?= c++
# -fPIC: Debian/Ubuntu build Qt5 with -reduce-relocations, which refuses to
# link into a non-PIC (-fPIE) binary. -fPIC is a harmless no-op elsewhere.
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -fPIC

# Prefer Qt6, fall back to Qt5.
QT_CORE  := $(shell pkg-config --exists Qt6Core && echo Qt6Core || echo Qt5Core)
CPPFLAGS += $(shell pkg-config --cflags $(QT_CORE))
LDLIBS   += $(shell pkg-config --libs $(QT_CORE))

test_wordcount: test_wordcount.cpp ../src/wordcount_logic.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) test_wordcount.cpp -o $@ $(LDLIBS)

# --- epub test: needs QtGui + its private headers -------------------------
QT_GUI        := $(shell pkg-config --exists Qt6Gui && echo Qt6Gui || echo Qt5Gui)
QT_GUI_VER    := $(shell pkg-config --modversion $(QT_GUI))
QT_GUI_PREFIX := $(shell pkg-config --variable=prefix $(QT_GUI))
QT_GUI_INC    := $(shell pkg-config --variable=includedir $(QT_GUI))

# Private headers live in versioned dirs. Layout differs by platform; list all
# candidates and let the compiler silently ignore the ones that don't exist.
PRIVATE_INC := \
  $(QT_GUI_INC)/QtGui/$(QT_GUI_VER) \
  $(QT_GUI_INC)/QtCore/$(QT_GUI_VER) \
  $(QT_GUI_PREFIX)/lib/QtGui.framework/Headers/$(QT_GUI_VER) \
  $(QT_GUI_PREFIX)/lib/QtCore.framework/Headers/$(QT_GUI_VER) \
  $(QT_GUI_PREFIX)/include/QtGui/$(QT_GUI_VER) \
  $(QT_GUI_PREFIX)/include/QtCore/$(QT_GUI_VER)

EPUB_CPPFLAGS := $(CPPFLAGS) $(shell pkg-config --cflags $(QT_GUI)) \
                 $(addprefix -I,$(PRIVATE_INC))
EPUB_LDLIBS   := $(shell pkg-config --libs $(QT_GUI)) $(LDLIBS)

test_epub_words: test_epub_words.cpp ../src/wordcount.cc ../src/wordcount.h \
                 ../src/wordcount_logic.h fixtures/sample.epub
	$(CXX) $(CXXFLAGS) $(EPUB_CPPFLAGS) test_epub_words.cpp ../src/wordcount.cc \
	    -o $@ $(EPUB_LDLIBS)

check: test_wordcount test_epub_words
	./test_wordcount
	./test_epub_words fixtures/sample.epub

clean:
	rm -f test_wordcount test_epub_words

.PHONY: check clean
