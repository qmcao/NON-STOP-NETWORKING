/**
 * nonstop_networking
 * CS 341 - Fall 2023
 */
#define _POSIX_C_SOURCE 200809L
#include "format.h"
#include "common.h"
#include "includes/dictionary.h"
#include "includes/vector.h"
#include <stdbool.h>
#include <signal.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <stdbool.h>
#include <dirent.h>

#define MAX_CLIENTS 100
#define PATH_MAX        4096

static char *temp_dir = NULL;
static char * port = NULL;
static vector* files_list;
static vector* server_file_list;
static dictionary *clients = NULL;
static dictionary * server_fd_size = NULL;
static int server_fd = -1;
static int client_fd = -1;
static int ep_fd = -1;


typedef struct client_status {
    verb cmd;
    char *filename;
    int status;
    char header[1024];
    size_t file_size;
    char *err_msg;
    int header_sent;
    int client_fd;
} client_status;

typedef struct server_file {
    size_t size;
    char *file_name;
    FILE* fp;
} server_file;
void signal_handler(int signal);
int read_header(client_status * client_request);
void update_epoll(client_status* cli, int cmd, int flag_header, char *remote_file_name);
void run_command(client_status* client_request);
int delete_handle(char *file_name);
int put_handle(char *file_name);
int list_handle();
int get_handle(char *file_name);
int create_connection(char *port);
void create_epoll();
void clean_up_server();
int remove_files_in_directory(const char *path);
int remove_directory(const char *path);
int main(int argc, char **argv) {
    // good luck!
    if (argc != 2) {
        print_server_usage();
        exit(1);
    }

    //TODO handle signal
    signal(SIGINT, signal_handler);
    signal(SIGPIPE, signal_handler);

    // set up temp dir
    char template[] = "XXXXXX";
    temp_dir = mkdtemp(template);   
    print_temp_directory(temp_dir);

    //
    port = argv[1]; // TODO free later
    files_list = vector_create(string_copy_constructor, string_destructor, string_default_constructor);
    clients = int_to_shallow_dictionary_create();
    server_fd_size = string_to_unsigned_long_dictionary_create();
    server_file_list = vector_create(NULL, NULL, NULL);
    //setup connection
    server_fd = create_connection(argv[1]);
    if (server_fd == -1) {
        //TODO clean up

        exit(1);

    }

    //create epoll
    create_epoll();
    clean_up_server();
    //free(header_buffer);
    return 0;

}

void signal_handler(int signal) {
    if (signal == SIGINT) {
        LOG("[+] Exiting..");
        clean_up_server();
        exit(1);
    }
}
int create_connection(char *port) {
    LOG("[+] Initializing server");
    //fprintf(stderr, "Initializing server 179\n");
    
    int socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (socket_fd == -1) {
        perror("socket err");
        return -1;
    }    
    struct addrinfo hints, *result;
    memset(&hints, 0, sizeof(struct addrinfo));
    
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int addinfo = getaddrinfo(NULL, port, &hints, &result);
    if (addinfo != 0) {
        fprintf(stderr, "%s", gai_strerror(addinfo));
        freeaddrinfo(result);
        close(socket_fd);
        return -1;
    }
    int opt = 1;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(socket_fd);
        freeaddrinfo(result);
        clean_up_server();
        exit(1);
    }
    if (bind(socket_fd, result->ai_addr, result->ai_addrlen) != 0) {
        perror("bind");
        close(socket_fd);
        freeaddrinfo(result);
        clean_up_server();
        return -1;
    }    
    if (listen(socket_fd, MAX_CLIENTS) == -1) {
        perror("listen()");
        freeaddrinfo(result);
        close(socket_fd);
        clean_up_server();
        exit(1);
    }    
    freeaddrinfo(result);
    //fprintf(stdout, "HELLo\n");
    return socket_fd;


}

void create_epoll() {
    ep_fd = epoll_create(1);
    if (ep_fd == -1) {
        perror("epoll err");
        clean_up_server();
        exit(1);
    }    
    struct epoll_event ev, events[MAX_CLIENTS];
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, server_fd, &ev) == -1) {
        perror("epoll_ct err");
        clean_up_server();
        exit(1);
    }
    //fprintf(stderr, "Initializing event notifications 107\n");
    while (1) {
        //fprintf(stdout, "-------------------------------\n");

        //fprintf(stdout, "Epoll before wait return\n");
        LOG("[+] Server listening for events...");
        int num_fds = epoll_wait(ep_fd, events, MAX_CLIENTS, -1);
        //fprintf(stdout, "Epoll wait return num_fds: %d\n", num_fds);
        if (num_fds == -1 ){
            perror("epol wait err");
            clean_up_server();
            exit(1);
        } else if (num_fds == 0) continue;
        // now go through list of ready fds to process if there is data
        for (int i = 0; i < num_fds; i++) {
            // fprintf(stdout, "num fd: %d\n", num_fds);
            // fprintf(stdout, "server_fd: %d\n", server_fd);
            // fprintf(stdout, "events[i].data.fd: %d\n", events[i].data.fd);

            if (events[i].data.fd == server_fd) {
                //fprintf(stdout, "PASS\n");
                int client_fd = accept(server_fd, NULL, NULL); 
                LOG("[+] Accept socket connection...");
                if (client_fd == -1 ) {
                    perror("accpet err");
                    clean_up_server();
                    exit(1);
                }
                ev.events = EPOLLIN;
                ev.data.fd = client_fd;
                if (epoll_ctl(ep_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1) {
                    perror("epoll_ctl err");
                    clean_up_server();
                    exit(1);
                } 
                client_status * new_connection = malloc(sizeof(client_status));
                new_connection->status = 0;
                new_connection->err_msg = "NULL";
                new_connection->file_size = 0;
                new_connection->header_sent = 0;
                new_connection->cmd = V_UNKNOWN;
                new_connection->client_fd = client_fd;
                dictionary_set(clients, &client_fd, new_connection);



                //TODO       
            } else {
                //printf("PASS\n");
                client_status * client_request = dictionary_get(clients, &(events[i].data.fd ));
                if (client_request -> header_sent == 0) {
                    // send header.
                    client_fd = events[i].data.fd;
                    int tmp = read_header(client_request);
                    if (tmp == 1) return;

                } else if (client_request-> header_sent == 1) {
                    //printf("PASS2\n");
                    //execute request.
                    run_command(client_request);
                    LOG("[+] Finish running command");
                    //break;
                    //exit(1); //!FOR NOW


                }

            }
            //fprintf(stdout," --------------------------\n");
        }
    //fprintf(stdout,"PASS\n");
    }
}
 

int read_header(client_status * client_request) {
    char *header_buffer  = calloc(1, sizeof(char)); 
    size_t total_bytes = 0;
    while (total_bytes < 1024) {
        size_t bytes_read = read(client_fd, (void *)(header_buffer + total_bytes), 1);
        if (bytes_read ==(size_t) -1 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }
        if (bytes_read == 0) {
            break;
        }
        if (bytes_read == (size_t) -1) {
            return 1;
        }

        total_bytes += bytes_read;
        if (header_buffer[total_bytes -1] == '\n') {
            break;
        }

    }
    //fprintf(stdout, "%zu\n",total_bytes);
    if (total_bytes >= 1024) {
        write_all_to_socket(client_fd, err_bad_request, strlen(err_bad_request));
        struct epoll_event event;
        event.events = EPOLLOUT;  // EPOLLIN==read, EPOLLOUT==write
        event.data.fd = client_fd;
        epoll_ctl(ep_fd, EPOLL_CTL_MOD, client_fd, &event);
        return 1;

    }


    if (total_bytes == 1) {
        clean_up_server();
        exit(1);
    }
    header_buffer[strlen(header_buffer) -1 ] = '\0'; 
    //fprintf(stdout, "Header: %s\n", header_buffer);
    //fprintf(stdout, "%zu\n", total_bytes);
    //fprintf(stdout, "%d\n", client_fd);
    //fprintf(stdout, "%d\n", server_fd);
    if (total_bytes >= 1024) {
        write_all_to_socket(client_fd, err_bad_request, strlen(err_bad_request));
        free(header_buffer);
        return 1;
        //TODO this line check again
    }
    ///TODO


    //printf("header: %s, strlen(header): %zu\n", header_buffer, strlen(header_buffer));
    if (strncmp(header_buffer, "LIST", 4) == 0) {
        update_epoll(client_request, LIST, 1, NULL);

        //fprintf(stdout, "%s\n", header_buffer);
    }
     else if(strncmp(header_buffer, "PUT", 3) == 0) {
        // fprintf(stdout, " File: %s\n",  header_buffer + 4);
        // fprintf(stdout, " length of the file: %zu\n",  strlen(header_buffer + 4));
        update_epoll(client_request, PUT, 1, header_buffer + 4);
        //client_request->cmd = PUT;
        //client_request->header_sent = 1;
        int len = strlen(temp_dir) + strlen(client_request->filename) + 2;
        //fprintf(stdout, "PASS\n");
        char buffer[len];
        strcpy(buffer, temp_dir);
        strcat(buffer, "/");
        strcat(buffer, client_request->filename);
        //printf("filepath: %s\n", buffer);
        //fprintf(stdout, "%s\n", header_buffer+4);
    } else if (strncmp(header_buffer, "GET", 3) == 0) {
        update_epoll(client_request, GET, 1, header_buffer + 4);
    } else if (strncmp(header_buffer, "DELETE", 6) == 0) {
        //fprintf(stdout, "File Name: %s\n", client_request->filename);
       // fprintf(stdout, "PASS HERE\n");
        //fprintf(stdout, "passing file: %s", header_buffer + 7);
        update_epoll(client_request, DELETE, 1, header_buffer + 7);
        



    } else {
        //printf("PASS\n");
        print_invalid_response();
        struct epoll_event event;
        event.events = EPOLLOUT;  // EPOLLIN==read, EPOLLOUT==write
        event.data.fd = client_fd;
        epoll_ctl(ep_fd, EPOLL_CTL_MOD, client_fd, &event);
        return 1;
    }
    struct epoll_event event;
    event.events = EPOLLOUT; 
    event.data.fd = client_fd;
    epoll_ctl(ep_fd, EPOLL_CTL_MOD, client_fd, &event);
    //free(header_buffer);
    return 0;

}



void update_epoll(client_status* cli, int cmd, int flag_header, char *remote_file_name) {
    cli->cmd = cmd;
    cli->header_sent = flag_header;
    cli->filename = remote_file_name;
}

void run_command(client_status* client_request) {
    verb cmd = client_request->cmd;
    if (cmd == DELETE) {
        //fprintf(stdout, "PASS\n");
        if (delete_handle(client_request->filename) == 0) {
            write_all_to_socket(client_fd, "OK\n", 3);
        } else {
            write_all_to_socket(client_fd, err_no_such_file, strlen(err_no_such_file));
        }
    } else if (cmd == PUT) {
        //return;                //!DELETE LATER 
        //printf("PASSasdasdasdasdasds\n");
        if (put_handle(client_request->filename) == 0) {
            write_all_to_socket(client_fd, "OK\n", 3);
            //exit(1);
            //TODO
        } else {
            return ;
            //TODO
        }
    } else if (cmd == LIST) {
        write_all_to_socket(client_fd, "OK\n", 3);
        if (list_handle() == 0) {
        } else {
            return ;
        }
    } else if (cmd == GET) {
        get_handle(client_request->filename);
    }
    epoll_ctl(ep_fd, EPOLL_CTL_DEL, client_fd, NULL);
    shutdown(client_fd, SHUT_RDWR);
    close(client_fd);
}

int delete_handle(char *file_name) {
    //LOG("delete IS CALLED");
    size_t total_length = strlen(temp_dir) + strlen(file_name) + 1; // + 1 for "/"
    char *buffer = malloc(total_length + 1);
    strcpy(buffer, temp_dir);
    strcat(buffer, "/");
    strcat(buffer, file_name);
    //fprintf(stdout, "%s\n", buffer);
    //fprintf(stdout, "%s\n", buffer);
    if (remove(buffer) == -1) {
        
        return 1;
    }
    int j = 0;
    for (size_t i = 0; i < vector_size(files_list); i++) {
        if (strcmp(vector_get(files_list, i), file_name) == 0) {
            vector_erase(files_list, i);
        }
        j++;
    }
    //fprintf(stdout, "server_file_list size: %zu\n", vector_size(server_file_list));
    for (size_t i = 0; i < vector_size(server_file_list); i++) {
        server_file* tmp = vector_get(server_file_list, i);
        //fprintf(stdout, "server_file_list->filename: %s, len: %zu\n", tmp->file_name , tmp->size );
        //fprintf(stdout, "filename: %s, len: %zu\n", file_name , strlen(file_name) );
        if (strcmp(tmp->file_name, file_name) == 0) {
            //printf("PASS\n");
            vector_erase(server_file_list, i);
            return 0;
        }
        j++;
    }
    return 1;

}   

int put_handle(char *file_name) {
    // fprintf(stdout, " File: %s\n",  file_name);
    // fprintf(stdout, " length of the file: %zu\n",  strlen(file_name));
    //get the header name to create file
    //LOG("Exec put"); //! DEL LATER

    size_t total_length = strlen(temp_dir) + strlen(file_name) + 1; // + 1 for "/"
    char file_path_buffer[total_length];
    strcpy(file_path_buffer, temp_dir);
    strcat(file_path_buffer, "/");
    strcat(file_path_buffer, file_name);  

    FILE *new_file = fopen(file_path_buffer, "w+");
    if (new_file == NULL) {
        perror("Fail to create file");
        return -1;
    }
    //fprintf(stdout, "PASS\n");

    // read the size of the file from the cline

   // char tmp_buf[1024];
    //read_all_from_socket(client_fd, tmp_buf, 1024);
    //printf("TMp buffer : %s\n", tmp_buf);


    size_t size;
    read_all_from_socket(client_fd, (char*) &size, sizeof(size_t));
    //printf("Package size: %zu\n", size);

    //now read from client socket

    size_t bytes_read = 0;
    //size_t current_bytes_left = 0;
    char buffer[1025];
   // LOG("here");
    while (bytes_read < size) {
        size_t available_to_read = size - bytes_read;
        if (available_to_read > 1024) {
            available_to_read = 1024;
        }
        size_t current_read = read_all_from_socket(client_fd, buffer, available_to_read);
        if (current_read == (size_t)-1 || current_read < available_to_read ) {
            continue;
        }
        fwrite(buffer, 1, current_read, new_file);
        bytes_read += current_read;
        if (current_read == 0) {
            //EOF
            break;
        }
        
    }
    
    //vector_push_back(files_list, file_name);
    //if file exist, overwrite -> look for server_file with same file_name, update its size, and file pointer maybe??

    for (size_t i = 0; i < vector_size(server_file_list); i++) {
        server_file *tmp =  vector_get(server_file_list, i);
        char *name = tmp->file_name;
        // file does exist
        if (strcmp(file_name, name) == 0) {
            LOG("[+] File exist, overwriting file...");
            tmp-> size = size;
            tmp->fp = new_file;
            fclose(new_file);
            return 0;
        }
    }


    server_file *new_file_instance_with_size = malloc(sizeof(server_file)); //!FREE LATER
    new_file_instance_with_size->file_name = file_name;
    new_file_instance_with_size->size = size;
    new_file_instance_with_size->fp = new_file;
    vector_push_back(server_file_list, new_file_instance_with_size);
    
    fclose(new_file);

    //fprintf(stdout, " server_vector_list_size: %zu\n", vector_size(server_file_list));

        
    // }

    //rmdir(temp_dir); //!DELETE THIS LATER
    //exit(1);// delete later
    return 0;  
}

int list_handle() {
    size_t total_size = 0;
    for(size_t i = 0; i < vector_size(server_file_list); i++) {
        server_file *file = vector_get(server_file_list, i);
        total_size += strlen(file->file_name) + 1; // + 1 for /n
        //free(file);
    }
    if (total_size > 0) {
        total_size--; // last line doesnt have /n
    }
    //fprintf(stdout, "TOtal size: %zu\n",(size_t) total_size);
    write_all_to_socket(client_fd, (char*) &total_size, sizeof(size_t) );

    for(size_t i = 0; i < vector_size(server_file_list); i++) {
        server_file *file = vector_get(server_file_list, i);
        //printf("passs\n");
        //printf("filename %s\n", file->file_name);
        char *filename = file->file_name;
        write_all_to_socket(client_fd, filename, strlen(filename));
        if (i != vector_size(server_file_list) -1 ) {
            //fprintf(stdout, "PASS\n");
            write_all_to_socket(client_fd, "\n", 1);
        }
        //free(file);
    }
    return 0;
}

int get_handle(char *file_name) {
    size_t total_length = strlen(temp_dir) + strlen(file_name) + 1; // + 1 for "/"
    char file_path_buffer[total_length];
    strcpy(file_path_buffer, temp_dir);
    strcat(file_path_buffer, "/");
    strcat(file_path_buffer, file_name);  

    FILE *server_file_to_read = fopen(file_path_buffer, "r");
    if (server_file_to_read == NULL) {
        write_all_to_socket(client_fd, err_no_such_file, strlen(err_no_such_file));
        clean_up_server();
        exit(1);
    }
    write_all_to_socket(client_fd, "OK\n", 3);
    size_t file_size = 0;
    for (size_t i = 0; i < vector_size(server_file_list); i++) {
        server_file *tmp = vector_get(server_file_list, i);
        char *tmp_name = tmp->file_name;
        if (strcmp(file_name, tmp_name) == 0) {
            file_size = tmp->size;
        }
    } 
    write_all_to_socket(client_fd, (char *)&file_size, sizeof(size_t));

    char buffer[1025];
    size_t total_bytes = 0;
    //size_t current_bytes_to_read = 0;
    while (total_bytes < file_size) {
        size_t avail_to_read = file_size - total_bytes;
        if (avail_to_read > 1024) {
            avail_to_read = 1024;
        }
        fread(buffer, 1, avail_to_read, server_file_to_read);
        //printf("available to read: %zu\n", avail_to_read);
        //printf("buffer data: %s\n", buffer);

        size_t bytes_written = write_all_to_socket(client_fd, buffer, avail_to_read);
        if (bytes_written < avail_to_read) {
            print_connection_closed();
            exit(1);
        }
        total_bytes += bytes_written;
    } 
    //printf("%zu\n", total_bytes);
    fclose(server_file_to_read);
    return 0;

}

void clean_up_server() {
    close(ep_fd);

    for (size_t i = 0; i < vector_size(server_file_list); i++) {
        server_file *tmp = vector_get(server_file_list, i);
        free(tmp);
    }
    vector_destroy(server_file_list);
    vector_destroy(files_list);

    remove_files_in_directory(temp_dir);
    rmdir(temp_dir);
    exit(1);
}
int remove_files_in_directory(const char *path) {
    DIR *dir;
    struct dirent *entry;

    // Open the directory
    dir = opendir(path);
    if (dir == NULL) {
        perror("Error opening directory");
        return 1;
    }

    // Iterate through each entry in the directory
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".." entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Construct the full path of the file
        char file_path[PATH_MAX];
        snprintf(file_path, sizeof(file_path), "%s/%s", path, entry->d_name);

        // Remove the file
        if (remove(file_path) != 0) {
            perror("Error removing file");
            closedir(dir);
            return 1;
        }

        LOG("Removed file: %s\n", file_path);
    }

    // Close the directory
    closedir(dir);

    return 0;
}