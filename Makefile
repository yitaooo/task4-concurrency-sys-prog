# Set you prefererred CFLAGS/compiler compiler here.
# Our github runner provides gcc-10 by default.
CC ?= cc
CFLAGS ?= -g -Wall -O2
CXX ?= c++
CXXFLAGS ?= -g -Wall -O0
CARGO ?= cargo
RUSTFLAGS ?= -g


.PHONY: all clean check

## Rust Example
#all:
#	cargo build

## C/C++ example
all: libcspinlock.so liblockhashmap.so liblockfreehashmap.so
libcspinlock.so: cspinlock.cpp
	$(CXX) $(CXXFLAGS) -shared -fPIC -ldl -o $@ $<

liblockhashmap.so: lockhashmap.cpp
	$(CXX) $(CXXFLAGS) -shared -o $@ $< -fPIC -ldl -Wl,-rpath,$(PWD) -L$(PWD) -lcspinlock

liblockfreehashmap.so: lockfreehashmap.cpp
	$(CXX) $(CXXFLAGS) -shared -fPIC -ldl -o $@ $<

# Usually there is no need to modify this
check: all
	$(MAKE) -C tests check

clean:
	$(MAKE) -C tests clean
	rm -rf *.so* *.o
