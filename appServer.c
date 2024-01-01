#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048
#define ALIAS_SZ 32
#define PORT 8080

// Client structure
typedef struct {
    struct sockaddr_in address;
    int sockfd;
    int uid;
    char alias[ALIAS_SZ];
} client_t;

client_t *clients[MAX_CLIENTS];
pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
int client_count = 0; // Track the number of clients
int uid = 0; // Unique identifier for clients

// Trim '\n' from string
void str_trim_lf (char* arr, int length) {
    int i;
    for (i = 0; i < length; i++) {
        if (arr[i] == '\n') {
            arr[i] = '\0';
            break;
        }
    }
}

// Print the IP address from a sockaddr_in structure
void print_client_addr(struct sockaddr_in addr) {
    printf("%d.%d.%d.%d",
           (addr.sin_addr.s_addr & 0xff),
           (addr.sin_addr.s_addr & 0xff00) >> 8,
           (addr.sin_addr.s_addr & 0xff0000) >> 16,
           (addr.sin_addr.s_addr >> 24) & 0xff);
}

// Add client to queue
void queue_add(client_t *cl){
    pthread_mutex_lock(&clients_mutex);
    for(int i = 0; i < MAX_CLIENTS; ++i) {
        if(!clients[i]) {
            clients[i] = cl;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Remove client from queue
void queue_remove(int uid){
    pthread_mutex_lock(&clients_mutex);
    for(int i = 0; i < MAX_CLIENTS; ++i) {
        if(clients[i]) {
            if(clients[i]->uid == uid) {
                clients[i] = NULL;
                break;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Send message to all clients except the sender
void send_message(char *s, int uid){
    pthread_mutex_lock(&clients_mutex);
    for(int i = 0; i < MAX_CLIENTS; ++i) {
        if(clients[i]) {
            if(clients[i]->uid != uid) {
                if(write(clients[i]->sockfd, s, strlen(s)) < 0) {
                    perror("ERROR: write to descriptor failed");
                    break;
                }
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

// Handle all communication with the client
void *handle_client(void *arg) {
    char buff_out[BUFFER_SZ];
    char buff_in[BUFFER_SZ / 2];
    int leave_flag = 0;
    client_t *cli = (client_t *)arg;

    // Name
    if(recv(cli->sockfd, buff_in, ALIAS_SZ, 0) <= 0 || strlen(buff_in) < 2 || strlen(buff_in) >= ALIAS_SZ-1) {
        printf("Didn't enter the name.\n");
        leave_flag = 1;
    } else {
        strcpy(cli->alias, buff_in);
        sprintf(buff_out, "%s has joined\n", cli->alias);
        printf("%s", buff_out);
        send_message(buff_out, cli->uid);
    }

    memset(buff_out, 0, BUFFER_SZ);

    while(1) {
        if (leave_flag) {
            break;
        }

        int receive = recv(cli->sockfd, buff_in, BUFFER_SZ, 0);
        if (receive > 0) {
            if(strlen(buff_in) > 0) {
                send_message(buff_in, cli->uid);
                str_trim_lf(buff_in, strlen(buff_in));
                printf("%s -> %s\n", cli->alias, buff_in);
            }
        } else if (receive == 0 || strcmp(buff_in, "exit") == 0) {
            sprintf(buff_out, "%s has left\n", cli->alias);
            printf("%s", buff_out);
            send_message(buff_out, cli->uid);
            leave_flag = 1;
        } else {
            perror("ERROR: -1");
            leave_flag = 1;
        }

        memset(buff_in, 0, BUFFER_SZ);
    }

    // Remove client from queue and yield thread
    close(cli->sockfd);
    queue_remove(cli->uid);
    free(cli);
    pthread_detach(pthread_self());

    return NULL;
}

int main() {
    int listenfd = 0, connfd = 0;
    struct sockaddr_in serv_addr;
    struct sockaddr_in cli_addr;
    pthread_t tid;

    // Socket settings
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    // Bind
    if(bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Socket binding failed");
        return EXIT_FAILURE;
    }

    // Listen
    if(listen(listenfd, 10) < 0) {
        perror("Socket listening failed");
        return EXIT_FAILURE;
    }

    printf("<[ SERVER STARTED ]>\n");

    // Accept clients
    while(1) {
        socklen_t clilen = sizeof(cli_addr);
        connfd = accept(listenfd, (struct sockaddr*)&cli_addr, &clilen);

        // Check if max clients is reached
        if((client_count + 1) >= MAX_CLIENTS) {
            printf("Max clients reached. Rejected: ");
            print_client_addr(cli_addr);
            printf(":%d\n", cli_addr.sin_port);
            close(connfd);
            continue;
        }

        // Client settings
        client_t *cli = (client_t *)malloc(sizeof(client_t));
        cli->address = cli_addr;
        cli->sockfd = connfd;
        cli->uid = uid++;
        client_count++;

        // Add client to the queue and fork thread
        queue_add(cli);
        pthread_create(&tid, NULL, &handle_client, (void*)cli);

        // Reduce CPU usage
        sleep(1);
    }

    return EXIT_SUCCESS;
}
