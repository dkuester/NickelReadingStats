/*
 * wordcount.h — epub word-counting helper for NickelReadingStats
 *
 * Counts words per spine item of a plain (reflowable) epub by reading the
 * epub zip directly. Returns one int per spine XHTML file, in reading order.
 * Empty list on any error.
 */
#pragma once

#include <QList>
#include <QString>

QList<int> count_epub_words(const QString &epub_path);
