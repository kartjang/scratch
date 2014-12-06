TEMPLATE    = app
TARGET      = runme
CONFIG      -= qt

#HEADERS += \
#    ThreadPool.h

#SOURCES += \
#    ThreadPool.cpp

#SOURCES += main.cpp

INCLUDEPATH += $${PWD}

HEADERS += lodepng/lodepng.h
SOURCES += lodepng/lodepng.cpp

SOURCES += test_ilim.cpp

# need these flags for gcc 4.8.x bug for threads
QMAKE_LFLAGS += -Wl,--no-as-needed
LIBS += -lpthread
QMAKE_CXXFLAGS += -std=c++11
