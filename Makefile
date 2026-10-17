include NickelHook/NickelHook.mk

TSM := vendor/libtsm

override PKGCONF  += Qt5Widgets
override LIBRARY  := src/libkoboterminal.so
override SOURCES  += src/koboterminal.cc src/term.cc \
                     $(wildcard $(TSM)/src/tsm/*.c) $(TSM)/src/shared/shl-htable.c $(TSM)/external/wcwidth/wcwidth.c
override CPPFLAGS += -I$(TSM)/src/tsm -I$(TSM)/src/shared -I$(TSM)/external -I$(TSM)/external/wcwidth
override CFLAGS   += -Wall -fvisibility=hidden -DSHL_EXPORT=
override CXXFLAGS += -Wall -Wextra -Werror -Wno-missing-field-initializers -fvisibility=hidden -fvisibility-inlines-hidden
override LDFLAGS  += -lutil
override KOBOROOT += res/koboterminal:/mnt/onboard/.adds/nm/koboterminal

include NickelHook/NickelHook.mk
