QT += core gui widgets

CONFIG += c++17
CONFIG -= app_bundle

TARGET = ui_process
TEMPLATE = app

# IPC库路径
IPC_PATH = ../..

# 源文件
SOURCES += main.cpp

# 头文件
INCLUDEPATH += $$IPC_PATH/include

# 源文件路径
SOURCES += \
    $$IPC_PATH/src/SharedDataPool.cpp \
    $$IPC_PATH/src/IPCEventCenter.cpp \
    $$IPC_PATH/src/DataPoolClient.cpp \
    $$IPC_PATH/src/SOERecorder.cpp \
    $$IPC_PATH/src/PersistentStorage.cpp

# 链接库
LIBS += -lrt -lpthread
