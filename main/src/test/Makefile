# Runs the emulated 6502 against Klaus Dormann's functional test suite.
#
#     make -C main/src/test run

CXX      ?= g++
CXXFLAGS ?= -std=gnu++11 -Wall -Wextra -O2 -g

SRCS = cpu_test.cpp ../CPU6502.cpp

.PHONY: run clean
run: cpu_test
	./cpu_test 6502_functional_test.bin

cpu_test: $(SRCS) ../CPU6502.hpp
	$(CXX) $(CXXFLAGS) -I.. -o $@ $(SRCS)

clean:
	rm -f cpu_test
