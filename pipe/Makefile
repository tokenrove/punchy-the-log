CFLAGS ?= -Wall -Wextra -std=gnu11 -g
tests != ls t/*.t

.PHONY: all clean check lint

all: producer consumer

producer: producer.o
consumer: consumer.o

clean:
	$(RM) producer consumer producer.o consumer.o

check: all $(tests)
	prove

lint:
	shellcheck -x $(tests)
