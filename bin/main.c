// bartosz_szpila
// 307554
#include <error.h>
#include <inttypes.h>

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <arpa/inet.h>
#include <netinet/ip.h>
#include <sys/socket.h>

#include "../include/udp_window.h"

#define INPUT_BASE 10
#define MAXPORT 65535

// Input order
#define ARG_IP 1
#define ARG_PORT 2
#define ARG_FNAME 3
#define ARG_SIZE 4

// UWAGA: w zadaniu nie sprecyzowano czy do pliku należy appendować
// File flags
// #define FILE_FLGS O_CLOEXEC|O_CREAT|O_APPEND
#define FILE_FLGS O_CREAT | O_TRUNC

int main(int argc, char** argv) {
  if (argc != 5) error(1, 0, "Arguments: ipaddr port file size\n");

  uint16_t port;
  int64_t size;
  int fd;
  struct sockaddr_in server_addr;

  /* Arguments
   * -----------------------------------------------------------------*/
  // Read the port and size
  {
    char* endptr;

    // port
    int read_num;
    read_num = strtol(argv[ARG_PORT], &endptr, INPUT_BASE);
    if (argv[ARG_PORT] == endptr || *endptr != '\0')
      error(1, 0, "Port string is incorrect");
    else if (read_num < 1 || read_num > MAXPORT)
      error(1, 0, "Port should be from 1 to %d\n", MAXPORT);
    port = (uint16_t)read_num;

    // size
    size = strtoll(argv[ARG_SIZE], &endptr, INPUT_BASE);
    if (argv[ARG_SIZE] == endptr || *endptr != '\0')
      error(1, 0, "Size string is incorrect\n");
    else if (size <= 0)
      error(1, 0, "Size should be more than 0\n");
  }

  // Prepare server address
  bzero(&server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(port);
  if (inet_pton(AF_INET, argv[ARG_IP], &server_addr.sin_addr.s_addr) != 1)
    error(1, 0, "Ip address string %s is incorrect\n", argv[ARG_IP]);

  // Open output file
  if ((fd = open(argv[ARG_FNAME], FILE_FLGS)) < 0)
    error(1, 0, "Couldnt open the file %s\n", argv[ARG_FNAME]);
  /*----------------------------------------------------------------------------*/
  server_read(server_addr, fd, size );
  return 0;
}
