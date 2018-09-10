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
#include <stdbool.h>
#include <ctype.h>
#include <sys/select.h>



void init_array(int * array, int length);
void add_clients_to_set(int * array, int length, fd_set * set);
int find_ready_fd(int * array, int length, fd_set * set);
void add_to_array(int * array, int length, int fd);
int max_fd_in_array(int * array, int length, int listenfd);
int guaranteed_write(int sockfd, char * packet, unsigned packet_length);
int guaranteed_read(int sockfd, char * packet, unsigned packet_length);
void remove_from_array(int * array, int length, int fd);
void cipher(unsigned char op, unsigned char shift, unsigned length, char * data);
void *get_in_addr(struct sockaddr *sa);
bool valid_checksum(char * packet, unsigned packet_length);
unsigned short calculate_checksum(char * packet, unsigned packet_length);
unsigned char alphabet_shift_and_modulo(unsigned char c, unsigned char shift);


int main(int argc, char * argv[]){
	//mine
	int max_clients = 200;
	int fd_array[200];
	char * port = argv[2];
	int listenfd;
	int maxfd; 
	int result;
	int tempfd;
	int newfd;
	struct timeval tv;
	// tv.tv_usec = 500000;

	//beej
    struct addrinfo hints, *server_info, *p;
   	struct sockaddr_storage client_addr;
    socklen_t clientlen;
    int yes = 1;
    char s[INET6_ADDRSTRLEN];


	/* Set up struct */
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; //use my ip.

    /* get info about itself. need to provide own address to bind function, which associates (ip,port) with fd. */
    int status = getaddrinfo(NULL, port, &hints, &server_info);
    if(status != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(status));
        return 1;
    }

    /* loop through all the results and bind to the first we can */
    for(p = server_info; p != NULL; p = p->ai_next) {
        if ((listenfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }
        //allows other sockets to bind to this port, unless there is active listening socket bound already.
        if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &yes,
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(listenfd);
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

    if (listen(listenfd, 3) == -1) {
        perror("listen");
        exit(1);
    }

    // printf("server: waiting for connections...\n");


    init_array(fd_array,max_clients);

    maxfd = listenfd;	//since most recently called.
    fd_set read_set;
    

    while(1){
    	/* Need to clear everytime. */
    	FD_ZERO(&read_set);	//clear set
    	FD_SET(listenfd, &read_set);	//add listener fd to read set so we can detect accepts.

    	/* Add the client sockets to set. */
    	add_clients_to_set(fd_array, max_clients, &read_set);

    	/* See which ones are ready to read. */
    	// tv.tv_sec = 5;
    	// tv.tv_usec = 500000;
    	result = select( max_fd_in_array(fd_array,max_clients,listenfd) + 1, &read_set, NULL, NULL, &tv);

    	if(result == -1){
    		perror("select");
    	}
    	else if (result == 0){
    		continue; //dunno if this is right...
    	}
    	else{	//there is stuff to read.

    		/* Need to call accept. */
    		if(FD_ISSET(listenfd, &read_set)){
    			clientlen = sizeof(struct sockaddr_storage);
    			newfd = accept(listenfd, (struct sockaddr *)&client_addr, &clientlen);
    			if(newfd == -1){
            		perror("accept");
           			// continue;
        		}
	    		/* print server info*/
	    		inet_ntop(client_addr.ss_family,
	            	get_in_addr((struct sockaddr *)&client_addr),
	            	s, sizeof s);
	        	// printf("server: got connection from %s\n", s);
        		add_to_array(fd_array, max_clients, newfd);
    		}




    		/* Iterate array of fds and see if we can read from any fd. */
    		tempfd = find_ready_fd(fd_array, max_clients, &read_set);
    		if(tempfd == -1){
    			continue;
    		}


    		/* DO I/O with temp_fd */
			int length;
            bool read_more = true;
            char * packet;
            char header[8];
            //new structure for code.
            while(1){

                /* Read header first */
                int header_read = guaranteed_read(tempfd, header, 8);
                if(header_read == -1){
                    close(tempfd);
                    exit(1);
                }

                /* Obtain length of packet. */
                unsigned packet_length;
                memcpy(&packet_length, header+4, 4);
                packet_length = ntohl(packet_length);

                /* Read entire packet now. */
                packet = calloc(1, packet_length);
                memcpy(packet, header, 8);
                if((length = guaranteed_read(tempfd, packet+8, packet_length-8)) == -1){
                    free(packet);
                    close(tempfd);
                    exit(1);
                }
                if(length + 8 < 10000000){
                    read_more = false;
                }



                /* Validate checksum */
                if(!valid_checksum(packet, packet_length)){
                    free(packet);
                    close(tempfd);
                    exit(1);
                }

                /* Get arguments */
                unsigned char op;
                unsigned char shift;
                memcpy(&op, packet, 1);
                memcpy(&shift, packet+1,1);

                /* Shift bytes */
                cipher(op, shift, packet_length, packet);

                /* Replace checksum */
                int zero = 0;
                memcpy(packet+2, &zero, 2); //zero checksum
                unsigned short checksum = calculate_checksum(packet, packet_length);
                memcpy(packet+2, &checksum, 2);

                /* Send data back */
                guaranteed_write(tempfd, packet, packet_length);

                free(packet);

                if(!read_more)
                    break;
            }
            remove_from_array(fd_array, max_clients, tempfd);
            close(tempfd);


    	}


    }
}

void init_array(int * array, int length){
	int i;
	for(i = 0; i < length; i++){
		array[i] = 0;
	}
}

void add_clients_to_set(int * array, int length, fd_set * set){
	int i;
	for(i = 0; i < length; i++){
		if(array[i]!= 0){
			FD_SET(array[i], set);
		}
	}
}

int find_ready_fd(int * array, int length, fd_set * set){
	int i;
	for(i = 0; i < length; i++){
		if(array[i] != 0){
			if(FD_ISSET(array[i],set)){
				return array[i];
			}
		}
		
	}
	//shouldn't be reached if called properly.
	return -1;
}

void add_to_array(int * array, int length, int fd){
	int i;
	for(i = 0; i < length; i++){
		if(array[i] == 0){
			array[i] = fd;
			return;
		}
	}
}

void remove_from_array(int * array, int length, int fd){
	int i;
	for(i = 0; i < length; i++){
		if(array[i] == fd){
			array[i] = 0;
			return;
		}
	}
}

int max_fd_in_array(int * array, int length, int listenfd){
	int i;
	int max = listenfd;
	for(i = 0; i < length; i++){
		if(array[i] > max){
			max = array[i];
		}
	}
	return max;
}




int guaranteed_write(int sockfd, char * packet, unsigned packet_length){
  int write_length = 0;
  int temp =0;

  while( write_length != packet_length){
    temp = write(sockfd, packet + write_length, packet_length-write_length);
    if(temp<0){
      // perror("write not working");
        return -1;
    }
    if(temp == 0){
      //EOF reached. this never happens for write.
      return write_length;
    }
    write_length += temp;
  }
  return packet_length;
}


int guaranteed_read(int sockfd, char * packet, unsigned packet_length){
  int read_length = 0;
  int temp = 0;

  while(read_length != packet_length){
    temp = read(sockfd, packet + read_length, packet_length - read_length);
    if(temp<0){
      // perror("read not working");
        return -1;
    }
    if(temp == 0){
      //EOF. wtf
      return read_length;
    }
    read_length += temp;
  }

  return read_length;
}


void cipher(unsigned char op, unsigned char shift, unsigned length, char * data){

    unsigned i;
    unsigned char c;

    if(op == 1){
        //decrypt
        int temp = shift * -1;
        while(temp < 0){
            temp += 26;
        }
        shift = (unsigned) temp;
    }


    for(i = 8; i < length; i++){
        c = *(data+i);
        if(isalpha(c)){
            c = tolower(c);
            c = alphabet_shift_and_modulo(c, shift);
            *(data+i) = c;
        }
    }
}


unsigned char alphabet_shift_and_modulo(unsigned char c, unsigned char shift){
    //97~122
    return (unsigned char)(((c - 97u + shift)%26u)+97u);
}

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

bool valid_checksum(char * packet, unsigned packet_length){
  unsigned short checksum = ~calculate_checksum(packet, packet_length);
  unsigned short validation = 0xffff;

  if(checksum == validation){
    return true;
  }
  return false;
}


unsigned short calculate_checksum(char * packet, unsigned packet_length){
  unsigned short checksum = 0;
  unsigned long long sum = 0;

  unsigned int carry;

  unsigned int i;

  /* Summation step */
  for(i = 0; i < packet_length; i = i+2){
    sum += (packet[i]<<8) & 0xff00;
    sum += (packet[i+1]) & 0xff;
  }

  /* Wrap-around: adding the carries */
  while(sum>>16){
    carry = (unsigned int)(sum>>16);
    sum = (sum & 0xffff);
    sum += carry;
  }

  /* One's comp */
  sum = ~sum;

  checksum = (unsigned short)(0xffff & sum);
  checksum = (unsigned short )htons(checksum);
  return checksum;
}









