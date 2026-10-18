# Host unit test for the pure word-counting logic.
# Builds against the HOST Qt (Qt5Core or Qt6Core) — no QZipReader, no cross
# toolchain, so it runs on any dev machine and in CI.

CXX      ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra

# Prefer Qt6Core, fall back to Qt5Core.
QT_CORE  := $(shell pkg-config --exists Qt6Core && echo Qt6Core || echo Qt5Core)
CPPFLAGS += $(shell pkg-config --cflags $(QT_CORE))
LDLIBS   += $(shell pkg-config --libs $(QT_CORE))

test_wordcount: test_wordcount.cpp ../src/wordcount_logic.h
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) test_wordcount.cpp -o $@ $(LDLIBS)

check: test_wordcount
	./test_wordcount

clean:
	rm -f test_wordcount

.PHONY: check clean
