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


#include <stdbool.h>


#define HEADER_SIZE 8

/* Function declarations */
unsigned adjust_to_even(unsigned packet_length);
unsigned short calculate_checksum(char * packet, unsigned packet_length);
bool valid_checksum(char * packet, unsigned packet_length);
void print_packet(char * packet, unsigned packet_length);
void *get_in_addr(struct sockaddr *sa);
int guaranteed_write(int sockfd, char * packet, unsigned packet_length);
int guaranteed_read(int sockfd, char * packet, unsigned packet_length);

//Client code- getaddrinfo, socket, connect.
/* ex: ./client -h 143.248.56.16 -p 5003 -o 1 -s 5 */
int main(int argc, char * argv[]){
  char * host = argv[2];
	char * port = argv[4];
	char * operation = argv[6];
	char * shift = argv[8];

	struct addrinfo hints;
	struct addrinfo *server_info, *p;

  int i;

	memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_UNSPEC;    //ipv4 ipv6 don't matter.
	//hints.ai_family = AF_INET;		
	hints.ai_socktype = SOCK_STREAM;	//stream socket type.

	int sockfd;
	int status = getaddrinfo(host, port, &hints, &server_info);
	if(status != 0){
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
  	exit(1);
	}

	//loop through results and connect to whichever that works- more robust?
	for(p = server_info; p!= NULL; p = p->ai_next){
		if( (sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
			perror("client: socket");    //error msg corresponding to errno.
      continue;
		}
    if( connect(sockfd, p->ai_addr, p->ai_addrlen) == -1){
      close(sockfd);
      perror("client: connect");
      continue;
    }
    //reach here upon success.
    break;
	}

  if(p == NULL){
    fprintf(stderr, "client: failed to connect");
    return 1; //beej returns 2?
  }

  /* check server information */
  char s[INET6_ADDRSTRLEN];
  //  inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
  inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof s);
  // printf("client: connecting to %s\n", s);

  freeaddrinfo(server_info);

  char * packet;
  char * packet_from_server;

  unsigned char op_byte;
  unsigned char shift_byte;
  unsigned packet_length;   //length of entire packet.
  unsigned original_length; //length of data before 16bit adjusted.
  unsigned data_length;          //length of data after 16bit adjusted.
  unsigned length_network_order;  //network order
  unsigned short checksum; //this needs to be 2 bytes later.

  bool read_more = true;

  int write_length;
  int read_length;

  int temp;

  /* Check shift and op. */
  op_byte = (unsigned char) atoi(operation);
  int temp_shift = atoi(shift);
  if(temp_shift > 255){
    temp_shift = temp_shift%26;
  }
  if(temp_shift < 0 ){ 
    while(temp_shift<0){
      temp_shift += 26;
    }
  }
  shift_byte = (unsigned char) temp_shift;
  if(op_byte != 0u && op_byte != 1u){
    close(sockfd);
    exit(1);
  }

  
  /* While loop for input longer than 10Mb */
  while(1){  
    packet = calloc(1,10000000);  //10MB(10x10^6) including header.
    if((original_length = read(0, packet+HEADER_SIZE, 10000000-HEADER_SIZE)) < 10000000-HEADER_SIZE){
      read_more = false;
    }


    /* Take care of errors */
    if( original_length == -1){
      perror("Error upon reading from stdin:");
    }

    /* Adjust Length to multiple of 16 */
    packet_length = adjust_to_even(original_length + HEADER_SIZE);
    data_length = packet_length - HEADER_SIZE;
    length_network_order = htonl(packet_length);

    /* Pack data */
    memcpy(packet, &op_byte, 1);
    memcpy(packet+1, &shift_byte, 1);
    memcpy(packet+4, &length_network_order, 4);

    /* Calculate checksum and place in field. */
    checksum = calculate_checksum(packet, packet_length);

    /* Pack data- checksum */
    memcpy(packet+2, &checksum, 2);

    /* Write to server */
    guaranteed_write(sockfd, packet, packet_length);

    /* Free packet sending to server. */
    free(packet);

    /* Receive from server */
    int read_length;
    packet_from_server = calloc(1, packet_length); 
    if(guaranteed_read(sockfd, packet_from_server, packet_length) < packet_length){
      //If less than what we sent comes back, terminate. 
      free(packet_from_server);
      close(sockfd);
      exit(1);
    }

    /* Validate checksum. If invalid, close connection and terminate. */
    if(!valid_checksum(packet_from_server, packet_length)){
      free(packet_from_server);
      close(sockfd);
      exit(1);
    }

    /* Write payload to stdout */  
    write(1, packet_from_server+8, packet_length-8);
    fflush(stdout);

    /* Free packet from server. */
    free(packet_from_server);

    if(!read_more)
      break;
  }

  close(sockfd);
  return 0;
}



/* For debugging */
void print_packet(char * packet, unsigned packet_length){
  int i;
  for(i =0; i<packet_length; i++){
    if(i%4 == 0){
      printf("\n");
    }
    printf("%02X ", *((char *)(packet+i)));
  }
  printf("\n");
}

unsigned adjust_to_even(unsigned packet_length){
  unsigned adjusted_length = packet_length;
  while(adjusted_length%2 != 0){
    adjusted_length++;
  }
  return adjusted_length;
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

void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int guaranteed_write(int sockfd, char * packet, unsigned packet_length){
  int write_length = 0;
  int temp =0;

  while( write_length != packet_length){
    temp = write(sockfd, packet + write_length, packet_length-write_length);
    if(temp<0){
      perror("write not working");
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
      perror("read not working");
    }
    if(temp == 0){
      //EOF. wtf
      return read_length;
    }
    read_length += temp;
  }

  return read_length;
}


















