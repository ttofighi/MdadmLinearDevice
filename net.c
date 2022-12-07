#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <err.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include "net.h"
#include "jbod.h"

/* the client socket descriptor for the connection to the server */
int cli_sd = -1;

/* attempts to read n (len) bytes from fd; returns true on success and false on failure. 
It may need to call the system call "read" multiple times to reach the given size len. 
*/
static bool nread(int fd, int len, uint8_t *buf) {
 int fdRead = 0; //holds number of bytes read
  while (fdRead < len){ //loop continues until we read len bytes
    int result = read(fd, &buf[fdRead], len - fdRead); //read from buffer
    
    if (result < 0){
      printf("err");
      return false;
    }
    else{
      fdRead += result; //increment bytes read
    }
  }
  return true;
}

/* attempts to write n bytes to fd; returns true on success and false on failure 
It may need to call the system call "write" multiple times to reach the size len.
*/
static bool nwrite(int fd, int len, uint8_t *buf) {
  int fdWrite = 0; //holds number of bytes written

  while(fdWrite < len){ //loop continues until we write len bytes
    int result = write(fd, &buf[fdWrite], len - fdWrite);
    if (result < 0){
      printf("error with nwrite");
      return false;
    }
    else{
      fdWrite += result; //increment bytes written
    }
  }
  return true;
}

/* Through this function call the client attempts to receive a packet from sd 
(i.e., receiving a response from the server.). It happens after the client previously 
forwarded a jbod operation call via a request message to the server.  
It returns true on success and false on failure. 
The values of the parameters (including op, ret, block) will be returned to the caller of this function: 

op - the address to store the jbod "opcode"  
ret - the address to store the info code (lowest bit represents the return value of the server side calling the corresponding jbod_operation function. 2nd lowest bit represent whether data block exists after HEADER_LEN.)
block - holds the received block content if existing (e.g., when the op command is JBOD_READ_BLOCK)

In your implementation, you can read the packet header first (i.e., read HEADER_LEN bytes first), 
and then use the length field in the header to determine whether it is needed to read 
a block of data from the server. You may use the above nread function here.  
*/
static bool recv_packet(int sd, uint32_t *op, uint8_t *ret, uint8_t *block) {
  uint8_t header[HEADER_LEN];

  //read the packet header first
  if (nread(sd, HEADER_LEN, header) == false){
    printf("error");
    return false;
  }

  //converts between host and network byte order
  *op = ntohl(*op);

  memcpy(op, &header, sizeof(*op)); //copy from start of the header
  memcpy(ret, &header[sizeof(*op)], sizeof(*ret));

  //check if block data exists by checking if the second lowest bit of ret is 1. if it is, read block
  //true means the second lowest bit is 1
  if(*ret == 2){
    if (nread(sd, JBOD_BLOCK_SIZE, block) == false){ //read from block
        return false;
    }
  }

  return true;
}

/* The client attempts to send a jbod request packet to sd (i.e., the server socket here); 
returns true on success and false on failure. 

op - the opcode. 
block- when the command is JBOD_WRITE_BLOCK, the block will contain data to write to the server jbod system;
otherwise it is NULL.

The above information (when applicable) has to be wrapped into a jbod request packet (format specified in readme).
You may call the above nwrite function to do the actual sending.  
*/
static bool send_packet(int sd, uint32_t op, uint8_t *block) {
  uint8_t packet[HEADER_LEN + JBOD_BLOCK_SIZE]; //5 and 256 bytes
  uint32_t cmd = op >> 12;
  uint8_t ret = 0;

  if (cmd == JBOD_WRITE_BLOCK){
    ret = 2;
  }
  
  else{
    ret = 0;
  }
  //conversion
  op = htonl(op);

  memcpy(&packet, &op, sizeof(op)); //write op to packet
  memcpy(&packet[sizeof(op)], &ret, sizeof(ret));
  
  //when command is JBOD_WRITE_BLOCK, write block to packet
  if (cmd == JBOD_WRITE_BLOCK){
      memcpy(&packet[HEADER_LEN], block, JBOD_BLOCK_SIZE);
      if(nwrite(sd, HEADER_LEN + JBOD_BLOCK_SIZE, packet) == false){ //write packet to server
        return false;
      }
    }
  else{
    return nwrite(sd, HEADER_LEN, packet);
  }
  return true;
}



/* attempts to connect to server and set the global cli_sd variable to the
 * socket; returns true if successful and false if not. 
 * this function will be invoked by tester to connect to the server at given ip and port.
 * you will not call it in mdadm.c
*/
bool jbod_connect(const char *ip, uint16_t port) {
  struct sockaddr_in caddr;
  int sock = socket(PF_INET, SOCK_STREAM, 0);

  //setup address info
  caddr.sin_family = AF_INET;
  caddr.sin_port = htons(port);

  if(sock == -1){
    printf("fail");
    return false;
  }

  inet_aton(ip, &caddr.sin_addr);
  cli_sd = sock;

  //connect socket file descriptor to the specified address
  if (connect(cli_sd, (const struct sockaddr *)&caddr, sizeof(caddr)) == -1){
    return false;
  }
  return true;
}




/* disconnects from the server and resets cli_sd */
void jbod_disconnect(void) {
  close(cli_sd);
  cli_sd = -1;
}



/* sends the JBOD operation to the server (use the send_packet function) and receives 
(use the recv_packet function) and processes the response. 

The meaning of each parameter is the same as in the original jbod_operation function. 
return: 0 means success, -1 means failure.
*/
int jbod_client_operation(uint32_t op, uint8_t *block) {
  uint8_t ret = 0;

  send_packet(cli_sd, op, block);
  recv_packet(cli_sd, &op, &ret, block);
  
  return 0;
}
