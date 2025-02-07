# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -Werror -O2 -g -I.
LIBS = -libverbs -lpthread -ldl

# Main program sources
SOURCES = common.c \
          send-receive/send_receive.c \
          rdma-write/rdma_write.c \
          rdma-read/rdma_read.c \
          lambda/lambda_client.c \
          lambda/lambda_server.c \
          rdma.c

# Main program objects
OBJECTS = $(SOURCES:.c=.o)

# Targets
all: rdma lambda-run.so

# Main RDMA executable
rdma: $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LIBS)

# Lambda function shared library
lambda-run.so: lambda-run.c
	$(CC) -shared -fPIC $(CFLAGS) -o $@ $<

# Generic object file compilation
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) rdma lambda-run.so

.PHONY: all clean