# Host side tests for the 6502 core. Two of them, and both have to pass:
#
#     make -C main/src/test run
#
# cpu_test runs Klaus Dormann's functional test suite, which checks what each
# instruction leaves behind and says nothing at all about how long it took.
# cycle_test checks the timing that suite is blind to, which is what fast
# loaders depend on. Run just one with `make klaus` or `make cycles`.

CXX      ?= g++
CXXFLAGS ?= -std=gnu++11 -Wall -Wextra -O2 -g

KLAUS_SRCS  = cpu_test.cpp ../CPU6502.cpp
CYCLE_SRCS  = cycle_test.cpp ../CPU6502.cpp

.PHONY: run klaus cycles clean
run: klaus cycles

klaus: cpu_test
	./cpu_test 6502_functional_test.bin

cycles: cycle_test
	./cycle_test

cpu_test: $(KLAUS_SRCS) ../CPU6502.hpp
	$(CXX) $(CXXFLAGS) -I.. -o $@ $(KLAUS_SRCS)

cycle_test: $(CYCLE_SRCS) ../CPU6502.hpp
	$(CXX) $(CXXFLAGS) -I.. -o $@ $(CYCLE_SRCS)

clean:
	rm -f cpu_test cycle_test
