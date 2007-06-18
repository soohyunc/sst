
# First include variables filled in by the configure script
!include(../../top.pri) {
        error("top.pri not found - please run configure at top level.")
}

contains($$list($$[QT_VERSION]), 4.1.*) {
   DEFINES += ETCDIR=\"$$ETCDIR\"
} else {
   DEFINES += ETCDIR=\\\"$$ETCDIR\\\"
}

TEMPLATE = app
TARGET = nsh
DESTDIR = ../..
DEPENDPATH += . ../../lib
INCLUDEPATH += . ../../lib
QT = core network
LIBS += -L../.. -lsst -lcrypto
POST_TARGETDEPS += ../../libsst.a
CONFIG -= app_bundle

# Input
HEADERS += cli.h ../proto.h ../asyncfile.h
SOURCES += main.cc cli.cc ../proto.cc ../asyncfile.cc

