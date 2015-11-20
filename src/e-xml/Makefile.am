AUTOMAKE_OPTIONS = foreign

ACLOCAL_AMFLAGS = -I m4

EXTRA_DIST =            \
src/parse_c.c           \
src/parse.cpp

lib_LTLIBRARIES = libe-xml.la

libe_xml_la_SOURCES =   \
src/epiphany_platform.h \
src/epiphany_xml_c.h    \
src/epiphany_xml.cpp    \
src/epiphany_xml.h      \
src/xml.cpp             \
src/xml.h

#lib_e_xml_la_LDFLAGS = -export-symbols-regex '^e_|^e_xml'
