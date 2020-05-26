#ifndef UDP_WINDOW_H
#define UDP_WINDO_H

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/ip.h>
#include <sys/socket.h>

#include <inttypes.h>

#define MAX_DATA 1000 * 1000  // server holds the queue of 1000 requests
#define BUF_DATA 1000
#define BUF_HEADER 24
#define BUF_SIZE BUF_HEADER + BUF_DATA

int64_t server_read(struct sockaddr_in server, int fd, uint64_t size);

#endif
