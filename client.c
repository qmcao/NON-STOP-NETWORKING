/**
 * nonstop_networking
 * CS 341 - Fall 2023
 */
#define _POSIX_C_SOURCE 200809L
#include "format.h"
#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
 #include <sys/socket.h>
#include "common.h"
#include <sys/types.h>
#include <netdb.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>

char **parse_args(int argc, char **argv);
verb check_args(char **args);
// my function

void connect_to_server(char *host, char *port);
void send_request(verb v);
void error_handler(size_t bytes_read, char* buffer);
int read_from_server();



static int g_argc;
static struct stat sb;
static char** local_arg;
static int server_fd = -1;
static FILE* local_file = NULL;
//static size_t file_size = (size_t)-1;
static verb v;
int main(int argc, char **argv) {
    // Good luck!
    g_argc = argc;
    local_arg = parse_args(argc, argv);
    v = check_args(local_arg);
    if (v == V_UNKNOWN) { 
        exit(1);
    }
    // makes sure the file it’s trying to upload (if it is uploading one) actually exists
    if (v == PUT) {
        if (access(local_arg[4], F_OK) == -1) {
            //print_error_message(err_no_such_file);
            // perror(local_arg[0]);
            // perror(local_arg[1]);
            // perror(local_arg[2]);
            // perror(local_arg[3]);
            // perror(local_arg[4]);
            // perror(local_arg[5]);
            perror("File does not exits");
            exit(1);
        }
    }
    // now connects to the server
    connect_to_server(local_arg[0], local_arg[1]);


    // sends the request (and file data, if needed) to the server
    send_request(v);
    shutdown(server_fd, SHUT_RD);
    close(server_fd);

    free(local_arg);
    return 0;
}


void connect_to_server(char *host, char *port) {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket err");
        exit(1);
    }
    struct addrinfo hints, *results;
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    int addr = getaddrinfo(host, port, &hints, &results);    
    if (addr != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(addr));
        exit(1);
    }
    if (connect(server_fd, results->ai_addr, results->ai_addrlen) == -1) {
        perror("connect err");
        exit(1);
    }
    freeaddrinfo(results);

}
/**
* Handles error response from server
*/
void error_handler(size_t bytes_read, char *buffer) {
    //fprintf(stdout, "%s\n", buffer);
    char* tmp_buffer = malloc(strlen("ERROR\n") + 1);
    //read_all_from_socket(server_fd, error_buff, strlen("ERROR\n"));
    //char * tmp = error_buff + bytes_read;
    read_all_from_socket(server_fd, tmp_buffer, strlen("ERROR\n") - bytes_read);
    //fprintf(stdout, "%s\n", error_buff);

    char * error_buff = malloc(strlen("ERROR\n") + 1);
    strcpy(error_buff, buffer);
    strcat(error_buff, tmp_buffer);
    //fprintf(stdout, "%s\n", error_buff);


    if (strcmp(error_buff, "ERROR\n") == 0) {
        size_t message_len = findMax(strlen(err_bad_file_size),strlen( err_bad_request),strlen( err_no_such_file)) ;
        
        char *message  = malloc(message_len + 1);
        if (read_all_from_socket(server_fd, message, message_len) == 0) {
            print_connection_closed();
        }
        print_error_message(message);
        //fprintf(stdout, "PASS\n");
        free(message);

    } else {
        //fprintf(stdout, "HELLO 141 \n");
        print_invalid_response();
    }
    free(error_buff);
    free(buffer);
    free(tmp_buffer);
    // for (int i = 0; i < g_argc; i++) {
    //     free(local_arg[i]);
    // }
    free(local_arg);
    exit(1);
    
}
// (local_arg[0]) : server
// (local_arg[1]): port
// (local_arg[2]) PUT/GET/DELETE/LIST
// (local_arg[3]) remote file
// (local_arg[4]) local file
void send_request(verb v) {


    size_t write_bytes = 0;
    size_t read_bytes = 0;
    //fprintf(stdout, "asdasddddddddddddddddd\n");
    //fprintf(stdout, "xxxxxxxxxx\n");
    char *s = NULL;
    //$ ./client server:port LIST
    int tmp = 1;
    if (v == LIST) {
        write_bytes = write_all_to_socket(server_fd, "LIST\n", 5);

        //fprintf(stdout, "%d\n", server_fd);
        if (write_bytes < strlen("LIST\n")) {
            perror("write err");
            print_connection_closed();
            exit(1);
        }

    } else {
        size_t total_len = strlen(local_arg[2])+strlen(local_arg[3]);
        total_len += 3;
        s = malloc(total_len);
        
        // Copy the first string to the result
        strcpy(s, local_arg[2]);

        // Append a space to the result
        strcat(s, " ");

        // Append the second string to the result
        strcat(s, local_arg[3]);
        strcat(s, "\n");
        //fprintf(stdout, "Request: %s\n",s);
        write_bytes = write_all_to_socket(server_fd, s, strlen(s));

        
    }

        // . Once the client has sent all the data to the server, it should perform a ‘half close’ by closing the write half of the socket (hint: shutdown()). 
        //This ensures that the server will eventually realize that the client has stopped sending data, and can move forward with processing the request.

    if (v == PUT) {
        //already check if file exsits

        stat(local_arg[4], &sb);
        size_t file_size = sb.st_size;
        // write size of package to socket.
        write_all_to_socket(server_fd, (char *) &file_size, sizeof(size_t));
        local_file = fopen(local_arg[4], "r");
        //printf(stdout, "%zu\n", file_size);
        size_t acceped_bytes = 0;
        size_t current_bytes_to_read = 0;
        char buffer[1025];
        while (acceped_bytes < file_size) {
            size_t available_bytes = file_size - acceped_bytes;
            if (available_bytes > 1024) {
                current_bytes_to_read = 1024;
            } else {
                // if available bytes < 1024
                current_bytes_to_read = available_bytes;
            }
            fread(buffer, 1, current_bytes_to_read, local_file);
            size_t bytes_written = write_all_to_socket(server_fd, buffer, current_bytes_to_read);
            if (bytes_written < current_bytes_to_read) {
                print_connection_closed();
                exit(1);
            }
            acceped_bytes += current_bytes_to_read;

        }
        fclose(local_file);
        free(s);
        print_success();
        exit(0);

    }
    

    if (shutdown(server_fd, SHUT_WR) != 0) {
        perror("shutdown err");
        exit(1);
    }




    // . The client should then read the response from the server and print it to stdout.
    //char *error_buff = malloc(strlen("ERROR\n") + 1);
    //read_all_from_socket(server_fd, error_buff, strlen("ERROR\n"));
    //fprintf(stdout, "%s\n", error_buff);

    char * buffer = malloc(strlen("OK\n") + 1); // !free later
    //fprintf(stdout, "HELLO1\n");
    read_bytes = read_all_from_socket(server_fd, buffer, strlen("OK\n"));
    // fprintf(stdout, "%s\n", buffer);

    // char tmp_Buffer[1024] = {0};
    // read_all_from_socket(server_fd, tmp_Buffer, strlen("ERROR\n") - read_bytes);
    // fprintf(stdout, "%s\n", tmp_Buffer);
    //fprintf(stdout, "HELLO2\n");

    //!!!!

    //fprintf(stdout, "%zu\n", read_bytes);
    //if buffer doesnt read OK response, then we need to handle error resopnse
    if (strcmp(buffer, "OK\n") != 0) {
        error_handler(read_bytes, buffer);

    }
    //!!!

    else if (strcmp(buffer, "OK\n") == 0) {
            //fprintf(stdout, "HELLO 195\n");
        if (v == LIST) {
            size_t size = 0;
            read_all_from_socket(server_fd, (char*)&size, sizeof(size_t));
            size_t tmp2 = (size + tmp)*1;
            char * buffer2 = malloc(tmp2);
            memset(buffer2, 0, tmp2);
            size_t bytes= tmp2 -1;
            read_bytes = read_all_from_socket(server_fd, buffer2, bytes);
            if (read_bytes < size) {
                print_too_little_data();
                exit(1);
            } else if (read_bytes > size) {
                print_received_too_much_data();
                exit(1);
            } else if (read_bytes == 0) {
                if (read_bytes != size) {
                    print_connection_closed();
                    exit(1);
                }
            }
            fprintf(stdout, "%s\n", buffer2);

            free(buffer2);


        } 
        else if (v == GET) {
            size_t rem_bytes = 0;
            local_file = fopen(local_arg[4], "a+");
            size_t size = 0;
            read_all_from_socket(server_fd, (char*)&size, sizeof(size_t));
            read_bytes = 0;
            char * buffer2 = NULL;
            buffer2 = malloc(1025);
            while (read_bytes < size) {
                memset(buffer2, 0, 1025);    
                rem_bytes = ((size - read_bytes) > 1024) ? 1024 : (size - read_bytes);
                read_bytes = read_all_from_socket(server_fd, buffer2, rem_bytes);
                fwrite(buffer2, 1, read_bytes, local_file);
                if (read_bytes == 0) break;

            }
            if (read_bytes < size) {
                //fprintf(stdout, "read bytes: %zu\n", read_bytes);
                //fprintf(stdout, "size: %zu\n", size);
                print_too_little_data();
                exit(1);
            } else if (read_bytes > size) {
                print_received_too_much_data();
                exit(1);
            } 
            fclose(local_file);
            free(buffer2);
        }   else if (v == DELETE) {
            print_success();
            return;
        }

    }
    free(buffer);
    free(s);
}


int read_from_server() {
    return 0;
}


/**
 * Given commandline argc and argv, parses argv.
 *
 * argc argc from main()
 * argv argv from main()
 *
 * Returns char* array in form of {host, port, method, remote, local, NULL}
 * where `method` is ALL CAPS
 */
char **parse_args(int argc, char **argv) {
    if (argc < 3) {
        return NULL;
    }

    char *host = strtok(argv[1], ":");
    char *port = strtok(NULL, ":");
    if (port == NULL) {
        return NULL;
    }

    char **args = calloc(1, 6 * sizeof(char *));
    args[0] = host;
    args[1] = port;
    args[2] = argv[2];
    char *temp = args[2];
    while (*temp) {
        *temp = toupper((unsigned char)*temp);
        temp++;
    }
    if (argc > 3) {
        args[3] = argv[3];
    }
    if (argc > 4) {
        args[4] = argv[4];
    }

    return args;
}

/**
 * Validates args to program.  If `args` are not valid, help information for the
 * program is printed.
 *
 * args     arguments to parse
 *
 * Returns a verb which corresponds to the request method
 */
verb check_args(char **args) {
    if (args == NULL) {
        print_client_usage();
        exit(1);
    }

    char *command = args[2];

    if (strcmp(command, "LIST") == 0) {
        return LIST;
    }

    if (strcmp(command, "GET") == 0) {
        if (args[3] != NULL && args[4] != NULL) {
            return GET;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "DELETE") == 0) {
        if (args[3] != NULL) {
            return DELETE;
        }
        print_client_help();
        exit(1);
    }

    if (strcmp(command, "PUT") == 0) {
        if (args[3] == NULL || args[4] == NULL) {
            print_client_help();
            exit(1);
        }
        return PUT;
    }

    // Not a valid Method
    print_client_help();
    exit(1);
}