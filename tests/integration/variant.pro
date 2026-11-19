# Test-only build of one limit-nvpstate source tree, for running under Wine.
#   qmake VARIANT_SRC=/path/to/limit-nvpstate/limit-nvpstate variant.pro
# Differences from the real MSVC build, identical for every variant:
#   - mingw + static Qt 5.15.2 instead of MSVC + Qt 5.14.2
#   - fake nvapi64.dll (mock/nvapi_mock.cpp) instead of NVIDIA's
#   - fgshim: emulates EVENT_SYSTEM_FOREGROUND, which Wine 9 never raises
TEMPLATE = app
TARGET   = limit-nvpstate
QT      += widgets
CONFIG  += c++17 release windows
CONFIG  -= debug_and_release

H = $$PWD
INCLUDEPATH += $$VARIANT_SRC $$H/include/casefix $$H/nvwrap $$H/include
SOURCES     += $$files($$VARIANT_SRC/*.cpp) $$H/mock/fgshim.cpp $$H/mock/testhook.cpp
HEADERS     += $$VARIANT_SRC/limitnvpstate.h $$VARIANT_SRC/about.h
FORMS       += $$VARIANT_SRC/limitnvpstate.ui $$VARIANT_SRC/about.ui
RESOURCES   += $$VARIANT_SRC/limitnvpstate.qrc
isEmpty(APPVER): APPVER = 5.3.0-dev
DEFINES     += WIN32_LEAN_AND_MEAN NOMINMAX _DEBUG APP_VERSION=$$APPVER LIMITNV_UPDATE_TEST LIMITNV_TEST_HOOKS
QMAKE_CXXFLAGS += -include $$H/include/fgshim.h -Wno-unknown-pragmas
LIBS        += -L$$H/bin -lnvapi64 -lshlwapi -ladvapi32 -luser32 -lwinhttp -lbcrypt
QMAKE_LFLAGS += -static -Wl,--allow-multiple-definition
