/**
 * nonstop_networking
 * CS 341 - Fall 2023
 */
#include "common.h"
#include "errno.h"
#include "stdio.h"
#include <string.h>
size_t write_all_to_socket(int socket, const char *buffer, size_t count) {
    size_t bytes_written = 0;
    //printf("HERE\n");
    while (bytes_written < count) {
       // printf("passs write all to sockettt\n");
        void *tmp = (void*) (buffer + bytes_written);
        ssize_t result = write(socket, tmp, count - bytes_written);
        if (result == 0) {
            return bytes_written;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        } else if (result < 0) {
            return result;
        }
        bytes_written += result;
        //fprintf(stdout, "PASS");
    }
    //printf("passs222 write all to sockettt\n");
    //printf("bytes_written: %zu\n", bytes_written);
    return bytes_written;
}

size_t read_all_from_socket(int socket,  char *buffer, size_t count) {
    size_t bytes_written = 0;
    while (bytes_written < count) {
        void *tmp = (void*) (buffer + bytes_written);
        ssize_t result = read(socket,tmp, count - bytes_written);
        //fprintf(stdout, "HELLO4\n");
        if (result == 0) {
            //fprintf(stdout, "bytes_written: %zu\n", bytes_written);
            return bytes_written;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        } else if (result < 0) {
            return -1;
        }
        bytes_written += result;
    }
    //fprintf(stdout, "%s\n", buffer);
    return bytes_written;
}

size_t findMax(size_t a, size_t b, size_t c) {
    size_t max = a;

    if (b > max) {
        max = b;
    }

    if (c > max) {
        max = c;
    }

    return max;
}

