TARGET = litedebugapi
TEMPLATE = lib

CONFIG += staticlib

include (../../liteideapi.pri)
include (../liteapi/liteapi.pri)

HEADERS += litedebugapi.h
