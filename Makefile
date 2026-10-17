include NickelHook/NickelHook.mk

override PKGCONF  += Qt5Widgets
override LIBRARY  := src/libkoboterminal.so
override SOURCES  += src/koboterminal.cc
override CFLAGS   += -Wall -Wextra -Werror -fvisibility=hidden
override CXXFLAGS += -Wall -Wextra -Werror -Wno-missing-field-initializers -fvisibility=hidden -fvisibility-inlines-hidden
override KOBOROOT += res/koboterminal:/mnt/onboard/.adds/nm/koboterminal

include NickelHook/NickelHook.mk
