#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <arpa/inet.h>


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
  printf("client: connecting to %s\n", s);

  freeaddrinfo(server_info);


  char * packet;
  unsigned char op_byte;
  unsigned char shift_byte;
  unsigned packet_length;   //length of entire packet.
  unsigned original_length; //length of data before 16bit adjusted.
  unsigned data_length;          //length of data after 16bit adjusted.
  unsigned length_network_order;  //network order
  unsigned short checksum = 0; //this needs to be 2 bytes later.

  int write_length;
  int read_length;

  packet = calloc(1,10000000);  //10MB(10x10^6) including header.
  op_byte = (unsigned char) atoi(operation);
  shift_byte = (unsigned char) atoi(shift);

  //Read from stdin into data area of packet.
  original_length = read(0, packet + 8, 10000000-4-4);
  /*what about when length is longer than 10MB?*/

  //Adjust length to multiple of 16.
  data_length = original_length;
  while((data_length+8)%16 != 0){
    data_length++;
  }
  packet_length = data_length+8;

  length_network_order = htonl(packet_length); 

  //Pack data.
  memcpy(packet, &op_byte, 1);
  memcpy(packet+1, &shift_byte, 1);
  memcpy(packet+2, &checksum, 2);
  memcpy(packet+4, &length_network_order, 4);

  //Calculate checksum and place in field.
  unsigned long long sum = 0;
  for(i =0; i < packet_length; i=i+2){
    sum += (packet[i]<<8) & 0xff00;
    //sum += (packet[i+1]) & 0xff;
    // printf("packet 1=%d\n", (packet[i]<<8) & 0xff00);
    sum += (packet[i+1]) & 0xff;//imagine bytes as stream when thinking of ordering.
    // printf("packet 1=%d\n", (packet[i+1]) & 0xff);
  }

  //Add the carries.
  int carry;
  while(sum>>16){
    //add carry to sum.
    //long long is 8 bytes.
    int carry = (int)(sum>>16);
    sum = (sum & 0x0000000000ffff);
    sum += carry;
  }
  sum = ~sum;

  checksum = (unsigned short)(0xffff & sum);
  // printf("What's checksum = 0x%x\n", checksum);

  checksum = htons(checksum);
  memcpy(packet+2, &checksum, 2);


  //Debugging print statements
  for(i =0; i<packet_length; i++){
    if(i%4 == 0){
      printf("\n");
    }
    printf("%02X ", *((char *)(packet+i)));
  }
  printf("\n");

  //write to server.
  write_length= write(sockfd, packet, packet_length);
  free(packet);

  printf("Finished writing to server\n");

  //Receive from server.
  char * data = calloc(1, packet_length);
  read_length = read(sockfd, data, packet_length);
  if(read_length < 0){
    perror("read not working");
  }

  //Debugging print statements
  for(i =0; i<packet_length; i++){
    if(i%4 == 0){
      printf("\n");
    }
    printf("%02X ", *((char *)(data+i)));
  }
  printf("\n");

  
  free(data);

  // printf("Finished\n");
}



