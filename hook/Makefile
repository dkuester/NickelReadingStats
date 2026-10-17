NH_QT_MAJOR ?= 6

include ./NickelHook/NickelHook.mk

override LIBRARY  := libreadingstats.so
override SOURCES  += src/readingstats.cc src/wordcount.cc
override CFLAGS   += -Wall -Wextra -Werror -Wno-missing-field-initializers -fvisibility=hidden
override CXXFLAGS += -Wall -Wextra -Werror -Wno-missing-field-initializers -fvisibility=hidden -fvisibility-inlines-hidden

# QZipReader lives in QtGui's private headers (used by src/wordcount.cc).
QT_GUI_VERSION := $(shell $(PKG_CONFIG) --modversion Qt$(NH_QT_MAJOR)Gui)
QT_GUI_PREFIX  := $(shell $(PKG_CONFIG) --variable=prefix Qt$(NH_QT_MAJOR)Gui)
override CXXFLAGS += -I$(QT_GUI_PREFIX)/include/QtGui/$(QT_GUI_VERSION) -I$(QT_GUI_PREFIX)/include/QtCore/$(QT_GUI_VERSION)

include ./NickelHook/NickelHook.mk
