QT -= gui
QT += core

TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
TARGET = test_justcanfd

INCLUDEPATH += .. ../../shared
SOURCES += test_justcanfd.cpp ../justcanfd.cpp
HEADERS += ../justcanfd.h ../../shared/dataengineinterface.h
