#!/bin/sh
# Create a file named binary.bin with the bytes:
# f o o \0 b a r

printf 'foo\0bar' > binary.bin
