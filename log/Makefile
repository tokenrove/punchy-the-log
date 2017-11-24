CFLAGS ?= -Wall -Wextra -std=gnu11 -g
tests != ls t/*.t

.PHONY: all clean check lint

all: producer consumer

producer: producer.o common.o
consumer: consumer.o common.o

clean:
	$(RM) producer consumer producer.o consumer.o common.o

check: all $(tests)
	prove

lint:
	shellcheck -x $(tests)
