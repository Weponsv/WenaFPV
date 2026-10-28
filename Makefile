CXX      ?= g++
# Skylake (i7-6500U) is the target; override with OPT=-march=native or OPT= for a generic build.
OPT      ?= -march=skylake -mtune=skylake
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++17 -Wall -Wextra $(OPT) $(shell sdl2-config --cflags)
LDLIBS   := $(shell sdl2-config --libs) -lGL

# Version
VERSION := 1.1.5_ALPHA

PREFIX ?= /usr
DESTDIR ?=

all: wenafpv

wenafpv: Makefile src/main.cpp src/sim.hpp src/world.hpp src/config.hpp
	$(CXX) $(CXXFLAGS) -o $@ src/main.cpp $(LDLIBS)

test: tests/physics_test
	./tests/physics_test

tests/physics_test: Makefile tests/physics_test.cpp src/sim.hpp src/config.hpp src/world.hpp
	$(CXX) -std=c++17 -O2 -Wall -Wextra -o $@ tests/physics_test.cpp

install: wenafpv
	install -Dm755 wenafpv $(DESTDIR)$(PREFIX)/bin/wenafpv
	install -Dm644 README.md $(DESTDIR)$(PREFIX)/share/doc/wenafpv/README.md
# After 'make clean && sudo make install' the binary above is (re)built as root.
# Hand it back to the invoking user so the next normal 'make' can overwrite it.
	@if [ -n "$$SUDO_USER" ] && [ "$$SUDO_USER" != "root" ]; then \
		chown "$$SUDO_USER" wenafpv 2>/dev/null || true; \
	fi

clean:
	rm -f wenafpv tests/physics_test

.PHONY: all test install clean
