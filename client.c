#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdbool.h>


#include <arpa/inet.h>

#define HEADER_SIZE 8


/* Function declarations */
unsigned adjust_to_multiple_of_16(unsigned packet_length);
unsigned short calculate_checksum(char * packet, unsigned packet_length);


void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}


//Client code- getaddrinfo, socket, connect.
/* ex: ./client -h 143.248.111.222 -p 1234 -o 0 -s 5 */
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

  //check server information
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

  op_byte = (unsigned char) atoi(operation);
  shift_byte = (unsigned char) atoi(shift);

  //Read from stdin into data area of packet.
  
  //while loop for input longer than 10MB.
  while(1){  
    packet = calloc(1,10000000);  //10MB(10x10^6) including header.
    if((original_length = read(0, packet+8, 10000000-HEADER_SIZE)) < 10000000-HEADER_SIZE){
      read_more = false;
    }

    /* Take care of errors */
    if( original_length == -1){
      perror("Error upon reading from stdin:");
    }

    /* Adjust Length to multiple of 16 */
    packet_length = adjust_to_multiple_of_16(original_length + HEADER_SIZE);
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

    /* Debugging - print packet */
    // for(i =0; i<packet_length; i++){
    //   if(i%4 == 0){
    //     printf("\n");
    //   }
    //   printf("%02X ", *((char *)(packet+i)));
    // }
    // printf("\n");

    /* Write to server */
    write_length = write(sockfd, packet, packet_length);

    /* Receive from server */
    packet_from_server = calloc(1, packet_length);  //+1 for null char.
    read_length = 0;
    int temp1;
    while(read_length != packet_length){
      temp1 = read(sockfd, packet_from_server+read_length, packet_length);
      if(temp1<0){
        perror("read not working");
      }
      read_length+=temp1;
    }


    /* Validate checksum. If invalid, close connection and terminate. */

    /* Debugging - print received packet */
    // for(i =0; i<packet_length; i++){
    //   if(i%4 == 0){
    //     printf("\n");
    //   }
    //   printf("%02X ", *((char *)(packet_from_server+i)));
    // }
    // printf("\n");

    /* May need to change this later. */  
    int temp2 = write(1, packet_from_server+8, packet_length-8);
    fflush(stdout);

    free(packet_from_server);

    // printf("\n");
    // printf("total sent to server = %d\n", write_length);
    // printf("total read from server = %d\n",read_length);
    // printf("total write to stdout = %d\n",temp2);

    /* Free and re calloc, to set to 0. */
    free(packet);

    if(!read_more){
      break;
    }
  }
}

unsigned adjust_to_multiple_of_16(unsigned packet_length){
  unsigned adjusted_length = packet_length;
  while(adjusted_length%2 != 0){
    adjusted_length++;
  }
  return adjusted_length;
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
    sum = (sum & 0x0000000000ffff);
    sum += carry;
  }

  /* One's comp */
  sum = ~sum;

  checksum = (unsigned short)(0xffff & sum);
  checksum = htons(checksum);
  return checksum;
}






