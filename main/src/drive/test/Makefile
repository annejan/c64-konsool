# Host side tests for the CBM DOS layer. No ESP-IDF needed:
#
#     make -C main/src/drive/test run

CXX      ?= g++
CXXFLAGS ?= -std=gnu++11 -Wall -Wextra -O1 -g -fsanitize=address,undefined

SRCS = dos_test.cpp ../CbmDos.cpp ../D64Disk.cpp

.PHONY: run clean
run: dos_test
	./dos_test

dos_test: $(SRCS) ../CbmDos.hpp ../D64Disk.hpp ../DiskImage.hpp ../IecDevice.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

clean:
	rm -f dos_test
