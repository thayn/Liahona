# Host test for the journey engine: `make` (needs gcc; uses the bundled cJSON).
# journey.json / roster.json here are the Manager's sample Leader Trial, compiled.
C = ../../cjson
test: test_journey
	./test_journey .
test_journey: test_journey.c ../journey.c $(C)/cJSON.c
	gcc -std=c11 -Wall -Wextra -Werror -O1 -I.. -I$(C) -o $@ $^
clean:
	rm -f test_journey
