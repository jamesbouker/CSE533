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
  char ip[MAXLINE];
  char filename[MAXLINE];

  int fd = open("client.in", O_RDONLY);
  printf("FD: %d\n", fd);

  Readline(fd, ip, MAXLINE);
  printf("ip: %s\n",ip);

  Readline(fd, line, MAXLINE);
  int port = atoi(line);
  printf("Port: %d\n",port);

  Readline(fd, filename, MAXLINE);
  printf("filename: %s\n",filename);

  Readline(fd, line, MAXLINE);
  int windowSize = atoi(line);
  printf("windowSize: %d\n",windowSize);

  Readline(fd, line, MAXLINE);
  int seed = atoi(line);
  printf("seed: %d\n",seed);

  Readline(fd, line, MAXLINE);
  float prob =  (float)strtod(line, NULL);
  printf("prob: %f\n",prob);


  Readline(fd, line, MAXLINE);
  int readingRate = atoi(line);
  printf("readingRate: %d\n",readingRate);

  close(fd);
}
