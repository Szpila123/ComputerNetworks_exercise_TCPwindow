// bartosz_szpila
// 307554

#include <errno.h>
#include <error.h>

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <stdlib.h>
#include <unistd.h>

#include <sys/select.h>

#include "../include/udp_window.h"

// helpful funcions
#define MIN(x, y) (((x) < (y) ? (x) : (y)))
#define ROUND_UP(x, y) (((x) + (y) + 1) / (y))
#define BIT(x) (1LL << (x))
#define SWAP_PTR(x, y) \
  ({                   \
    void* _tmp = (x);  \
    (x) = (y);         \
    (y) = _tmp;        \
  })

// windown buffer defines
#define GET_BUF(win, x) ((win).buf[((win).buf_first + (x)) % (win).RWS])

#define TURN_SEC 1
#define TURN_USEC 700000
#define TRUE 1
#define FALSE 0
typedef struct {
  struct {
    void* read_buf;  // buffer to receive packets
    void** buf;      // buffers for data
    int buf_first;   // index of first buffer in queue
  };
  struct {
    int RWS;        // window size (count of buffers)
    int fst_frame;  // next frame
    int frame_cnt;  // count of frames
    char* done;     // mask of fetched data
  };
} Window_t;

// create udp socket
static inline int crt_soc(void);

// create starting window structure
static inline Window_t crt_window(uint64_t size);

// free memory taken by window buffers
static inline void kill_window(Window_t win);

// check if frame was received
static inline int check_frame(Window_t win, int frame);

// set that frame was received
static inline void set_frame(Window_t win, int frame);

// send request for unreceived frames (in window)
static void send_requests(Window_t win, struct sockaddr_in server, int sockfd,
                          uint64_t size);

// recevie data sent by server
static int receive_frame(struct sockaddr_in server, Window_t win, int fd,
                         int sockfd);

/* DEFINITIONS */
int64_t server_read(struct sockaddr_in server, int fd, uint64_t size) {
  // create sending socket
  int snd_soc = crt_soc();

  // create window structure
  Window_t win = crt_window(size);

  struct timeval tv;
  uint64_t bytes_recv = 0;

  fd_set descriptors;
  int ready;

  // main loop
  while (bytes_recv < size) {
    // send requests
    send_requests(win, server, snd_soc, size);

    // set time to wait for answers
    tv.tv_sec = TURN_SEC;
    tv.tv_usec = TURN_USEC;

    FD_ZERO(&descriptors);
    FD_SET(snd_soc, &descriptors);
    while ((ready = select(snd_soc + 1, &descriptors, NULL, NULL, &tv)) != 0) {
      if (ready == -1) {
        if (errno == EINTR)
          continue;
        else
          error(1, 0, "UDP_WINDOW: Select failed\n");
      }

      bytes_recv += receive_frame(server, win, fd, snd_soc);
    }
  }
  kill_window(win);
  return bytes_recv;
}

static int receive_frame(struct sockaddr_in server, Window_t win, int fd,
                         int sockfd) {
  // receive the packet
  struct sockaddr_in sender;
  socklen_t sender_len = sizeof(sender);
  ssize_t frame_len = recvfrom(sockfd, win.read_buf, BUF_SIZE, 0,
                               (struct sockaddr*)&sender, &sender_len);
  if (frame_len == -1)
    error(1, 0, "UDP_WINDOW: Error occured while receving the frame\n");

  // drop packet if sender is not the server
  if (sender.sin_addr.s_addr != server.sin_addr.s_addr ||
      sender.sin_port != server.sin_port)
    return 0;

  // read the start
  char* tok = strchr((char*)win.read_buf, ' ');
  int start = strtol(tok + 1, NULL, 10);
  int index = start / BUF_DATA;

  // if already got the fream, drop the packet
  if (check_frame(win, index)) return 0;

  // set fream as received
  set_frame(win, index);

  // if frame wasnt first in the window, move it to buffer and continue
  if (index != win.fst_frame) {
    SWAP_PTR(win.read_buf, GET_BUF(win, index));
    return 0;
  }

  int recv = 0;
  // if it was save buffered prefix to the file
  while (index != win.frame_cnt && check_frame(win, index)) {
    tok = strchr((char*)win.buf[index], ' ');
    tok = strchr(tok + 1, ' ');
    int size = strtol(tok + 1, NULL, 10);
    tok = strchr(tok + 1, '\n');

    write(fd, (void*)(tok + 1), size);
    recv += size;

    // move the buffers and window
    win.buf_first++;
    win.fst_frame++;
    index = win.fst_frame;
  }
  return recv;
}

static void send_requests(Window_t win, struct sockaddr_in server, int sockfd,
                          uint64_t size) {
  for (int i = win.fst_frame; i < win.fst_frame + win.RWS && i < win.frame_cnt;
       i++) {
    // check if frame was received
    if (check_frame(win, i)) continue;

    // frame request data
    char data[64];
    int req_size = size - i * BUF_DATA >= BUF_DATA ? BUF_DATA : i % BUF_DATA;
    int data_size = sprintf(data, "GET %d %d\n", i * BUF_DATA, req_size);

    // send request
    if (sendto(sockfd, data, data_size, 0, (struct sockaddr*)&server,
               sizeof(server)) < 0)
      error(1, 0, "UDP_WINDOW: Error while sending UDP frame\n");
  }
  return;
}

static inline Window_t crt_window(uint64_t size) {
  Window_t win = (Window_t){
      .buf = NULL,
      .read_buf = NULL,
      .buf_first = 0,
      .RWS = MIN((int)MAX_DATA / BUF_DATA, (int)size / BUF_DATA + 1),
      .fst_frame = 0,
      .frame_cnt = (int)ROUND_UP(ROUND_UP(size, BUF_DATA), sizeof(char)),
      .done = NULL};

  // create array of buffer pointers
  win.buf = (void**)malloc(sizeof(void*) * win.RWS);
  win.read_buf = malloc(BUF_DATA + BUF_HEADER);
  if (win.buf == NULL || win.read_buf == NULL)
    error(1, 0, "UDP_WINDOW: Couldn't allocate space for buffers\n");

  // allocate space for buffers
  for (int i = 0; i < win.RWS; i++) {
    win.buf[i] = malloc(BUF_DATA + BUF_HEADER);
    if (win.buf[i] == NULL)
      error(1, 0, "UDP_WINDOW: Couldn't allocate space for buffers\n");
  }

  // allocate space for done array
  win.done = (char*)malloc(win.frame_cnt);
  bzero(win.done, win.frame_cnt);
  return win;
}

static inline int crt_soc(void) {
  int soc = socket(AF_INET, SOCK_DGRAM, 0);

  if (soc == -1) error(1, 0, "UDP_WINDOW: Couldn't create the socket\n");

  return soc;
}

static inline void kill_window(Window_t win) {
  for (int i = 0; i < win.RWS; i++) free(win.buf[i]);
  free(win.buf);
  free(win.done);
  free(win.read_buf);
  return;
}

static inline int check_frame(Window_t win, int frame) {
  int index = frame / sizeof(char);
  int bit = frame % sizeof(char);
  if (win.done[index] & BIT(bit)) return TRUE;
  return FALSE;
}

static inline void set_frame(Window_t win, int frame) {
  int index = frame / sizeof(char);
  int bit = frame % sizeof(char);
  win.done[index] |= BIT(bit);
  return;
}
