NH_QT_MAJOR ?= 6

include ./NickelHook/NickelHook.mk

override LIBRARY  := libreadingstats.so
override SOURCES  += src/readingstats.cc
override CFLAGS   += -Wall -Wextra -Werror -Wno-missing-field-initializers -fvisibility=hidden
override CXXFLAGS += -Wall -Wextra -Werror -Wno-missing-field-initializers -fvisibility=hidden -fvisibility-inlines-hidden

include ./NickelHook/NickelHook.mk
