/*
 * Starter code for proxy lab.
 * Feel free to modify this code in whatever way you wish.
 * Andrew ID: ziyuehua
 */

/* Some useful includes to help you get started */

#include "cache.h"
#include "csapp.h"
#include "http_parser.h"

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>

/*
 * Debug macros, which can be enabled by adding -DDEBUG in the Makefile
 * Use these if you find them useful, or delete them if not
 */
#ifdef DEBUG
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_printf(...) fprintf(stderr, __VA_ARGS__)
#else
#define dbg_assert(...)
#define dbg_printf(...)
#endif

/*
 * Max cache and object sizes
 * You might want to move these to the file containing your cache implementation
 */
#define MAX_CACHE_SIZE (1024 * 1024)
#define MAX_OBJECT_SIZE (100 * 1024)
#define HOSTLEN 256
#define SERVLEN 8
#define CHUNK_SIZE 4096

/* Typedef for convenience */
typedef struct sockaddr SA;

/* Information about a connected client. */
typedef struct {
    struct sockaddr_in addr; // Socket address
    socklen_t addrlen;       // Socket address length
    int connfd;              // Client connection file descriptor
    char host[HOSTLEN];      // Client host
    char serv[SERVLEN];      // Client service (port)
} client_info;

/* URI parsing results. */
typedef enum { PARSE_ERROR, PARSE_STATIC, PARSE_DYNAMIC } parse_result;

/*
 * String to use for the User-Agent header.
 * Don't forget to terminate with \r\n
 */
static const char *header_user_agent = "Mozilla/5.0"
                                       " (X11; Linux x86_64; rv:3.10.0)"
                                       " Gecko/20220411 Firefox/63.0.1\n";

void sigpipe_handler(int sig) {
    // Simply ignore the signal, no logging or action required
    return;
}

void send_501_not_implemented(int clientfd) {
    const char *response =
        "HTTP/1.0 501 Not Implemented\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 135\r\n"
        "\r\n"
        "<html><head><title>501 Not Implemented</title></head>"
        "<body><h1>501 Not Implemented</h1>"
        "<p>The requested method is not supported by this proxy "
        "server.</p></body></html>";

    rio_writen(clientfd, response, strlen(response));
}

/*
 * serve - handle one HTTP request/response transaction
 */
void *serve(void *vargp) {
    client_info *client = (client_info *)vargp;
    pthread_detach(pthread_self());
    // Initiate client RIO and parser
    rio_t rio;
    parser_state state;
    parser_t *parser = parser_new();
    if (parser == NULL) {
        fprintf(stderr, "Failed to initialize parser\n");
        return NULL;
    }
    rio_readinitb(&rio, client->connfd);

    // Read request line
    char buf[MAXLINE];
    while (1) {
        ssize_t n = rio_readlineb(&rio, buf, MAXLINE);
        if (n == 0) {
            // EOF: Client closed connection before completing the request
            fprintf(stderr, "Client closed the connection before sending the "
                            "complete request\n");
            parser_free(parser);
            return NULL;
        } else if (n < 0) {
            // Error during read
            fprintf(stderr, "Error reading from client socket\n");
            parser_free(parser);
            return NULL;
        }

        if (strcmp(buf, "\r\n") == 0 || strcmp(buf, "\n") == 0) {
            // End of headers
            break;
        }

        state = parser_parse_line(parser, buf);
        if (state == ERROR) {
            fprintf(stderr, "Error parsing line: %s\n", buf);
            parser_free(parser);
            return NULL;
        }
    }

    // Initialize values
    const char *method = NULL;
    const char *uri = NULL;
    const char *host = NULL;
    const char *port = NULL;
    // const char *path = NULL;
    const char *http_version = NULL;

    if (parser_retrieve(parser, METHOD, &method) == 0) {
        printf("Method: %s\n", method);
    } else {
        if (strcmp(method, "GET") != 0) {
            send_501_not_implemented(client->connfd);
            parser_free(parser);
            return NULL;
        } else {
            fprintf(stderr, "METHOD not implemented\n");
            parser_free(parser);
            return NULL;
        }
    }

    if (parser_retrieve(parser, URI, &uri) == 0) {
        printf("URI: %s\n", uri);
    } else {
        fprintf(stderr, "Failed to retrieve URI\n");
    }

    if (parser_retrieve(parser, HOST, &host) == 0) {
        printf("Host: %s\n", host);
    } else {
        fprintf(stderr, "Failed to retrieve HOST\n");
    }

    if (parser_retrieve(parser, HTTP_VERSION, &http_version) == 0) {
        printf("HTTP Version: %s\n", http_version);
    } else {
        fprintf(stderr, "Failed to retrieve HTTP_VERSION\n");
    }

    // Default to port 80 if no port is specified in the Host
    if (parser_retrieve(parser, PORT, &port) != 0) {
        port = "80";
    }

    // Check whether the result is already in cache
    void *cached_data = NULL;
    int cached_size = 0;

    if (get_cache_node(uri, &cached_data, &cached_size) == 0 &&
        strcmp(method, "GET") == 0) {
        // Step 4: Serve the cached response to the client
        rio_writen(client->connfd, cached_data, cached_size);
        printf("Served from cache: %s\n", uri);
        close(client->connfd);
        free(client);
        return NULL;
    }
    fflush(stdout);

    // Step 4: Establish connection with remote server based on client request
    // Connect to the remote server using open_clientfd
    int serverfd;
    serverfd = open_clientfd(host, port);
    if (serverfd < 0) {
        fprintf(stderr, "Failed to connect to remote server: %s:%s\n", host,
                port);
        parser_free(parser);
        close(serverfd);
        close(client->connfd);
        return NULL;
    }

    // Step 5: Forward the request to the remote server
    rio_t server_rio;
    rio_readinitb(&server_rio, serverfd);

    // Forward the parsed request line
    snprintf(buf, MAXLINE, "%s %s HTTP/1.0\r\n", method, uri);
    if (rio_writen(serverfd, buf, strlen(buf)) < 0) {
        fprintf(stderr, "Lost server connection\n");
        close(serverfd);
        close(client->connfd);
        parser_free(parser);
        return NULL;
    }

    // Forward each header
    const char *header_name = NULL;
    const char *header_value = NULL;
    header_t *header;

    while ((header = parser_retrieve_next_header(parser)) != NULL) {
        header_name = header->name;
        header_value = header->value;
        snprintf(buf, MAXLINE, "%s: %s\r\n", header_name, header_value);
        if (rio_writen(serverfd, buf, strlen(buf)) < 0) {
            fprintf(stderr, "Lost server connection\n");
            close(serverfd);
            close(client->connfd);
            parser_free(parser);
            return NULL;
        }
    }

    // End headers with an empty line
    snprintf(buf, MAXLINE, "\r\n");
    if (rio_writen(serverfd, buf, strlen(buf)) < 0) {
        close(serverfd);
        parser_free(parser);
        return NULL;
    }

    // Step 6: Read the response from the server and relay it back to client
    ssize_t n;
    char response[MAX_OBJECT_SIZE];
    char *response_ptr = response;
    int total_size = 0;

    while ((n = rio_readnb(&server_rio, buf, CHUNK_SIZE)) > 0) {
        if (rio_writen(client->connfd, buf, n) < 0) {
            // Client closed connection while server is sending data
            // Handle possible SIGPIPE issue and cleanup
            fprintf(stderr,
                    "Client closed connection while sending response\n");
            break;
        }

        total_size += n;

        // Only copy data when total_size < MAX
        if (total_size <= MAX_OBJECT_SIZE) {
            memcpy(response_ptr, buf, n);
            response_ptr += n;
        }
    }

    if ((total_size < MAX_OBJECT_SIZE) && (method = "GET")) {
        add_cache_node(uri, response, total_size);
        printf("Cached response for: %s\n", uri);
    }

    close(serverfd);
    close(client->connfd);
    free(client);
    return NULL;
}

int main(int argc, char **argv) {
    int listenfd;
    // Register sigpipe_handler
    signal(SIGPIPE, sigpipe_handler);
    // Initialize cache
    init_cache();
    // char buffer[BUFFER_SIZE];
    printf("%s", header_user_agent);

    // Initialize the proxy
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]); // Convert the command line argument to an integer

    if (port <= 0) {
        fprintf(stderr, "Invalid port number: %d\n", port);
        return 1;
    }

    listenfd = open_listenfd(argv[1]);
    if (listenfd < 0) {
        fprintf(stderr, "Failed to listen on port: %s\n", argv[1]);
        exit(1);
    }

    // Accept connect request from clients continuously
    while (1) {
        /* Allocate space on the stack for client info */
        client_info *client = malloc(sizeof(client_info));
        if (client == NULL) {
            perror("malloc");
            continue;
        }

        /* Initialize the length of the address */
        client->addrlen = sizeof(client->addr);

        /* accept() will block until a client connects to the port */
        client->connfd =
            accept(listenfd, (SA *)&client->addr, &client->addrlen);
        if (client->connfd < 0) {
            perror("accept");
            continue;
        }

        // Update client info, and print out
        int res = getnameinfo((SA *)&client->addr, client->addrlen,
                              client->host, sizeof(client->host), client->serv,
                              sizeof(client->serv), 0);
        if (res == 0) {
            printf("Accepted connection from %s:%s\n", client->host,
                   client->serv);
        } else {
            fprintf(stderr, "getnameinfo failed: %s\n", gai_strerror(res));
        }

        // Create a new thread to handle the client connection
        pthread_t tid;
        if (pthread_create(&tid, NULL, serve, client) != 0) {
            perror("pthread_create");
            free(client); // Free memory if thread creation fails
            close(client->connfd);
            continue;
        }
        pthread_detach(tid);
    }
    free_cache();
    return 0;
}
