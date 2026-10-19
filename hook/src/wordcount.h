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

// Uncompressed size limit per zip entry. A larger entry counts as unreadable
// (0 words for a chapter, no result for container.xml/OPF): counting holds
// several copies of an entry at once, and the size comes from the zip
// header, so this also stops a zip bomb before it is inflated.
constexpr qint64 kMaxEpubEntryBytes = 8 * 1024 * 1024;

QList<int> count_epub_words(const QString &epub_path,
                            qint64 max_entry_bytes = kMaxEpubEntryBytes);
