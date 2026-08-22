CC = gcc
CFLAGS = -Wall -Wextra -Werror -g -I. -I../include
OBJS = test_hid_gamepad.o hid_gamepad.o hid_layout.o

test_hid_gamepad: $(OBJS)
	$(CC) -o $@ $(OBJS)

test_hid_gamepad.o: test_hid_gamepad.c test_descriptors.h test_reports.h ../include/hid_gamepad.h esp_log.h

hid_gamepad.o: ../src/hid_gamepad.c ../include/hid_gamepad.h ../include/hid_layout.h
	$(CC) $(CFLAGS) -c -o $@ $<

hid_layout.o: ../src/hid_layout.c ../include/hid_layout.h
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: test
test: test_hid_gamepad
	./test_hid_gamepad

.PHONY: clean
clean:
	rm -f *.o test_hid_gamepad
