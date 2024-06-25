/**
 * nonstop_networking
 * CS 341 - Fall 2023
 */
#pragma once
#include <stddef.h>
#include <sys/types.h>
#include <unistd.h>
#define LOG(...)                      \
    do {                              \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n");        \
    } while (0);

typedef enum { GET, PUT, DELETE, LIST, V_UNKNOWN } verb;
size_t write_all_to_socket(int socket, const char *buffer, size_t count);
size_t read_all_from_socket(int socket, char *buffer, size_t count);
size_t findMax(size_t a, size_t b, size_t c);
ssize_t read_header_from_socket(int socket, char *buffer, size_t count);