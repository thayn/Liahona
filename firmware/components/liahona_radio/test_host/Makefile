# Host tests for the radio code: `make` (needs gcc). No hardware or ESP-IDF required.
R = ..
B = ../../../../base_station/main
test: test_radio
	./test_radio
test_radio: test_radio.c $(R)/sx1262.c $(R)/liahona_proto.c $(B)/bridge.c
	gcc -std=c11 -Wall -Wextra -Werror -O1 -I$(R) -I$(B) -o $@ $^ -lm
clean:
	rm -f test_radio
