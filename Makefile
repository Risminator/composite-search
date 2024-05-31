CFLAGS=-g -Wall -Wextra -Werror -O2
TARGETS=lab1vjdN3250 libvjdN3250.so

.PHONY: all clean

all: $(TARGETS)

clean:
	rm -rf *.o $(TARGETS)

lab1vjdN3250: lab1vjdN3250.c plugin_api.h
	gcc $(CFLAGS) -o lab1vjdN3250 lab1vjdN3250.c -ldl

libvjdN3250.so: libvjdN3250.c plugin_api.h
	gcc $(CFLAGS) -shared -fPIC -o libvjdN3250.so libvjdN3250.c -ldl -lm
