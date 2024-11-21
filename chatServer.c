//Hananel Sabag 
#include "chatServer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <ctype.h>


#define MAX_CLIENTS 30
#define BUFFER_SIZE 4096

int sockfd;
conn_pool_t pool;

// Handles SIGINT (Ctrl+C) signal: closes all client connections and the server socket, then exits.
void handleSigint(int sig) {
    printf("Server shutting down.\n");

    conn_t *current = pool.conn_head;
    while (current != NULL) {
        removeConn(current->fd, &pool);
        current = pool.conn_head; // Start from the new head after removal
    }

    close(sockfd);
    exit(0);
}

// Initializes the connection pool structure, setting up for tracking client connections and communication.
int initPool(conn_pool_t *pool) {
    pool->maxfd = -1;
    FD_ZERO(&pool->read_set);
    FD_ZERO(&pool->ready_read_set);
    FD_ZERO(&pool->write_set);
    FD_ZERO(&pool->ready_write_set);
    pool->conn_head = NULL;
    pool->nr_conns = 0;
    return 0; // Success
}


// Sets up the server socket, binds it to a port, and listens for incoming connections.
void setupServerSocket(int port) {
    struct sockaddr_in serverAddr;
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("Setsockopt failed");
        exit(EXIT_FAILURE);
    }

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(port);

    if (bind(sockfd, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, MAX_CLIENTS) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    // Set server socket to non-blocking mode
    int flags = fcntl(sockfd, F_GETFL, 0);
    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

    printf("Server listening on port %d\n", port);
    // Initialize the connection pool
    initPool(&pool);
}

// Accepts new client connections if under the maximum limit and adds them to the connection pool.
void acceptNewConnections() {
    struct sockaddr_in clientAddr;
    socklen_t addrlen = sizeof(clientAddr);
    int new_socket;

    if (pool.nr_conns >= MAX_CLIENTS) {
        printf("Maximum number of connections (%d) reached. New connections will be temporarily rejected.\n",
               MAX_CLIENTS);
        return; // Temporarily stop accepting server is "full"
    }

    while ((new_socket = accept(sockfd, (struct sockaddr *) &clientAddr, &addrlen)) >= 0) {
        int flags = fcntl(new_socket, F_GETFL, 0);
        if (fcntl(new_socket, F_SETFL, flags | O_NONBLOCK) == -1) {
            perror("Error setting socket to non-blocking");
            close(new_socket); // Close the socket if setting to non-blocking fails
            continue;
        }

        printf("New incoming connection - socket fd is %d, ip is: %s, port: %d\n", new_socket,
               inet_ntoa(clientAddr.sin_addr), ntohs(clientAddr.sin_port));

        if (addConn(new_socket, &pool) == -1) {
            perror("Failed to add new connection");
            close(new_socket);
        } else {
            FD_SET(new_socket, &pool.read_set);
            if (new_socket > pool.maxfd) pool.maxfd = new_socket;
        }
    }
}


// Adds a message to the message queue of all connected clients except the sender.
int addMsg(int sd, char *buffer, int len, conn_pool_t *pool) {


    for (int i = 0; i < len; ++i) {
        buffer[i] = toupper(buffer[i]);
    }

    for (conn_t *conn = pool->conn_head; conn != NULL; conn = conn->next) {
        if (conn->fd == sd) continue; // Skip the sender

        msg_t *newMsg = (msg_t *) malloc(sizeof(msg_t));
        if (!newMsg) return -1; // Memory allocation failed

        newMsg->message = (char *) malloc(len + 1);
        if (!newMsg->message) {
            free(newMsg);
            return -1;
        }
        memcpy(newMsg->message, buffer, len);
        newMsg->message[len] = '\0';
        newMsg->size = len;
        newMsg->next = NULL;
        newMsg->prev = NULL;

        if (conn->write_msg_tail) {
            conn->write_msg_tail->next = newMsg;
            newMsg->prev = conn->write_msg_tail;
            conn->write_msg_tail = newMsg;
        } else {
            conn->write_msg_head = conn->write_msg_tail = newMsg;
        }
    }
    return 0;
}

// Attempts to send queued messages to a client. If not all data can be sent, the message is adjusted for next attempt.
int writeToClient(int sd, conn_pool_t *pool) {
    conn_t *conn = pool->conn_head;
    while (conn != NULL) {
        if (conn->fd == sd) {
            msg_t *msg = conn->write_msg_head;
            while (msg != NULL) {
                ssize_t sent = send(conn->fd, msg->message, msg->size, 0);
                if (sent == -1) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        // Socket is not ready for sending more data, try again later.
                        break; // Exit the loop and try again later
                    } else {
                        // An error occurred, print error and consider closing the connection.
                        perror("Failed to send message to client");
                        return -1;
                    }
                }

                // Successfully sent part or all of the message
                if (sent < msg->size) {
                    char *new_message = malloc(msg->size - sent);
                    if (new_message == NULL) {
                        perror("Memory allocation failed");
                        return -1;
                    }
                    memcpy(new_message, msg->message + sent, msg->size - sent);

                    free(msg->message);
                    msg->message = new_message;
                    msg->size -= sent;
                } else {
                    // Message fully sent, move to next message
                    conn->write_msg_head = msg->next;
                    if (conn->write_msg_head == NULL) {
                        conn->write_msg_tail = NULL; // Queue is now empty
                    }

                    // Free the sent message
                    free(msg->message);
                    free(msg);
                    msg = conn->write_msg_head;
                }
            }
            break; // Exit after processing the designated connection
        }
        conn = conn->next;
    }
    return 0;
}


// Handles incoming messages from clients, reads data, broadcasts it, and closes connections if clients disconnect.
void receiveAndBroadcastMessages() {
    char buffer[BUFFER_SIZE];
    ssize_t valread;
    conn_t *current_conn = pool.conn_head;
    conn_t *next_conn;

    while (current_conn != NULL) {
        next_conn = current_conn->next; // Save next connection before potential removal

        if (FD_ISSET(current_conn->fd, &pool.ready_read_set)) {
            printf("Descriptor %d is readable\n", current_conn->fd);
            // Attempt to read from this connection
            valread = recv(current_conn->fd, buffer, BUFFER_SIZE - 1, 0);

            if (valread > 0) {
                buffer[valread] = '\0'; // Ensure null-termination
                printf("%zd bytes received from sd %d: %s\n", valread, current_conn->fd, buffer);

                if (addMsg(current_conn->fd, buffer, valread, &pool) == -1) {
                    perror("Failed to add message to other connections");
                }
            } else if (valread == 0 || (errno != EWOULDBLOCK && errno != EAGAIN)) {
                // Client disconnected or error occurred
                printf("Connection closed for sd %d\n", current_conn->fd);
                removeConn(current_conn->fd, &pool);
            }
        }

        current_conn = next_conn; // Proceed to the next connection safely
    }

    // After processing read set, check write set for each connection to send queued messages
    current_conn = pool.conn_head;
    while (current_conn != NULL) {
        next_conn = current_conn->next; // Save next connection

        if (FD_ISSET(current_conn->fd, &pool.ready_write_set)) {
            writeToClient(current_conn->fd, &pool);
        }

        current_conn = next_conn; // Move to the next connection
    }
}

// Adds a new client connection to the pool and prepares it for communication.
int addConn(int sd, conn_pool_t *pool) {
    conn_t *newConn = (conn_t *) malloc(sizeof(conn_t));
    if (newConn == NULL) {
        return -1;
    }

    newConn->fd = sd;
    newConn->write_msg_head = NULL;
    newConn->write_msg_tail = NULL;
    newConn->prev = NULL;
    newConn->next = pool->conn_head;

    if (pool->conn_head != NULL) {
        pool->conn_head->prev = newConn;
    }
    pool->conn_head = newConn;

    if (sd > pool->maxfd) {
        pool->maxfd = sd;
    }

    pool->nr_conns++;

    return 0;
}

// Removes a client connection from the pool, cleans up, and closes the socket.
int removeConn(int sd, conn_pool_t *pool) {
    conn_t **ptr = &pool->conn_head;

    while (*ptr != NULL) {
        if ((*ptr)->fd == sd) {
            conn_t *temp = *ptr;
            *ptr = (*ptr)->next;

            if (temp->next != NULL) {
                temp->next->prev = temp->prev;
            }
            if (temp->prev != NULL) {
                temp->prev->next = temp->next;
            }

            while (temp->write_msg_head != NULL) {
                msg_t *msg = temp->write_msg_head;
                temp->write_msg_head = msg->next;
                free(msg->message);
                free(msg);
            }
            printf("removing connection with sd %d \n", sd);
            close(temp->fd);
            free(temp);

            pool->nr_conns--; // Decrement the number of active connections

            return 0;
        }
        ptr = &(*ptr)->next;
    }
    return -1;
}


int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: server <port>\n");
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        printf("Port number must be between 1 and 65535.\n");
        exit(EXIT_FAILURE);
    }

    signal(SIGINT, handleSigint);
    setupServerSocket(port);

    while (1) {
        // Reset read and write sets
        FD_ZERO(&pool.read_set);
        FD_ZERO(&pool.write_set);

        // Add the server socket to read_set
        FD_SET(sockfd, &pool.read_set);
        pool.maxfd = sockfd; // Initially, the maxfd is the sockfd

        // Add client sockets to the read_set and possibly write_set
        for (conn_t *conn = pool.conn_head; conn != NULL; conn = conn->next) {
            FD_SET(conn->fd, &pool.read_set);
            if (conn->write_msg_head != NULL) {
                FD_SET(conn->fd, &pool.write_set);
            }
            if (conn->fd > pool.maxfd) {
                pool.maxfd = conn->fd; // Update maxfd if necessary
            }
        }

        // Prepare the subset of fds that are ready
        pool.ready_read_set = pool.read_set;
        pool.ready_write_set = pool.write_set;

        printf("Waiting on select()...\nMaxFd %d\n", pool.maxfd);
        // Call select
        if (select(pool.maxfd + 1, &pool.ready_read_set, &pool.ready_write_set, NULL, NULL) < 0) {
            perror("select error");
            exit(EXIT_FAILURE);
        }

        // Check if there's a new connection
        if (FD_ISSET(sockfd, &pool.ready_read_set)) {
            acceptNewConnections();
        }

        // Handle IO operations for existing connections
        receiveAndBroadcastMessages();
    }

}