/*
** server.c -- a stream socket server demo
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#define PORT "5001"  // the port users will be connecting to

#define BACKLOG 10     // how many pending connections queue will hold

void cipher(int op, unsigned shift, unsigned length, char * data);
void sigchld_handler(int s)
{
    // waitpid() might overwrite errno, so we save and restore it:
    int saved_errno = errno;

    while(waitpid(-1, NULL, WNOHANG) > 0);

    errno = saved_errno;
}


// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(void)
{

    int sockfd;
    int newfd;
    struct addrinfo hints, *server_info, *p;
    struct sockaddr_storage client_addr;
    socklen_t clientlen;

    int yes = 1;
    char s[INET6_ADDRSTRLEN];

    struct sigaction sa;


    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; //use my ip.

    //get info about itself. need to provide own address to bind function, which associates (ip,port) with fd.
    int status = getaddrinfo(NULL, PORT, &hints, &server_info);
    if(status != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = server_info; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        //COME BACK
        //allows other sockets to bind to this port, unless there is active listening socket bound already.
        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("server: bind");
            continue;
        }
        break;
    }

    freeaddrinfo(server_info);

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }


    //COME BACK
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("server: waiting for connections...\n");

    while(1){
        clientlen = sizeof(struct sockaddr_storage);
        newfd = accept(sockfd, (struct sockaddr *)&client_addr, &clientlen);   
        if(newfd == -1){
            perror("accept");
            continue;
        }

        inet_ntop(client_addr.ss_family,
            get_in_addr((struct sockaddr *)&client_addr),
            s, sizeof s);
        printf("server: got connection from %s\n", s);

        if(!fork()){    //inside is child process.
            close(sockfd);  //doesn't need listener fd.

            //do stuff
            int op;
            unsigned shift;
            unsigned length;
            int checksum;

            //
            read(newfd, &op, 1);
            read(newfd, &shift, 1);
            read(newfd, &checksum, 2);
            read(newfd, &length, 4);
            length = ntohl(length);

            void * data = malloc(length);
            read(newfd, data, length);

            printf("testing: op = %d, shift = %u, length = %u, data = %s\n", op, shift, length, data);

            cipher(op, shift, length, (char *)data);
            
            //just send data itself back!
            write(newfd, data, length);

            close(newfd);
            exit(0);
        }
        close(newfd);
    }

    return 0;
}


void cipher(int op, unsigned shift, unsigned length, char * data){

    char shift_char = (char) shift;  //ok since shift is not negative.

    if(op == 1){//decryption
        shift_char = shift_char * -1;
    } 

    for(int i = 0; i < length; i++){
        *(data+i) = (*(data+i)) + shift_char;
    }
}























