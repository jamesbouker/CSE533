#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include  "unp.h"

#pragma mark - Main

int main(int argc, char **argv) {
  char line[MAXLINE];

  int fd = open("server.in", O_RDONLY);
  printf("FD: %d\n", fd);

  Readline(fd, line, MAXLINE);
  int port = atoi(line);
  printf("Port: %d\n",port);

  Readline(fd, line, MAXLINE);
  int windowSize = atoi(line);
  printf("windowSize: %d\n",windowSize);

  close(fd);
}
