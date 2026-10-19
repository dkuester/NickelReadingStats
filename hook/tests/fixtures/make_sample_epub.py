#!/usr/bin/env python3
"""Regenerate fixtures/sample.epub for test_epub_words.

Spine (expected word counts in brackets):
  c1   ch1.xhtml            [4]
  c2   ch2.xhtml            [2]
  gone missing.xhtml        [0]  manifest entry without a zip file
  c3   ch3.xhtml            [3]
  c4   Text/Kapitel%204.xhtml [2] subfolder + percent-encoded href
                                 -> "OEBPS/Text/Kapitel 4.xhtml"

Every chapter has a <head><title> whose words must NOT be counted.

Usage: python3 make_sample_epub.py [output.epub]
"""

import sys
import zipfile

CONTAINER = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles>
    <rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>
  </rootfiles>
</container>
"""

OPF = """<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="bookid">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:identifier id="bookid">fixture-001</dc:identifier>
    <dc:title>Word Count Fixture</dc:title>
  </metadata>
  <manifest>
    <item id="c1" href="ch1.xhtml" media-type="application/xhtml+xml"/>
    <item id="c2" href="ch2.xhtml" media-type="application/xhtml+xml"/>
    <item id="gone" href="missing.xhtml" media-type="application/xhtml+xml"/>
    <item id="c3" href="ch3.xhtml" media-type="application/xhtml+xml"/>
    <item id="c4" href="Text/Kapitel%204.xhtml" media-type="application/xhtml+xml"/>
  </manifest>
  <spine>
    <itemref idref="c1"/>
    <itemref idref="c2"/>
    <itemref idref="gone"/>
    <itemref idref="c3"/>
    <itemref idref="c4"/>
  </spine>
</package>
"""


def xhtml(body):
    return ('<?xml version="1.0" encoding="UTF-8"?>\n'
            '<html xmlns="http://www.w3.org/1999/xhtml">\n'
            "<head><title>Chapter Title Words</title></head>\n"
            "<body>%s</body>\n</html>\n" % body)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "sample.epub"

    def add(z, name, data, compress=zipfile.ZIP_DEFLATED):
        # Fixed timestamp so regenerating yields a byte-identical file.
        info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
        info.compress_type = compress
        z.writestr(info, data)

    with zipfile.ZipFile(out, "w") as z:
        # mimetype must be the first entry and stored uncompressed.
        add(z, "mimetype", "application/epub+zip", zipfile.ZIP_STORED)
        add(z, "META-INF/container.xml", CONTAINER)
        add(z, "OEBPS/content.opf", OPF)
        add(z, "OEBPS/ch1.xhtml", xhtml("<p>alpha beta gamma delta</p>"))
        add(z, "OEBPS/ch2.xhtml", xhtml("<p>epsilon zeta</p>"))
        add(z, "OEBPS/ch3.xhtml", xhtml("<p>eta theta iota</p>"))
        add(z, "OEBPS/Text/Kapitel 4.xhtml", xhtml("<p>kappa lambda</p>"))

if __name__ == "__main__":
    main()
