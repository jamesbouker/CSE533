#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "unpifiplus.h"

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

    int sockfd;
    struct sockaddr_in servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);
    
    ip[strlen(ip)-1] = (char)NULL;
    printf("ip: %s \n", ip);
    int len = strlen(ip);
    printf("strlen: %d\n", len);
    int i;
    for(i=0;i<len;i++) {
        if(ip[i] == '\n')
            printf("GOTCHYOU");
        printf("Value: %c\n", (char)ip[i]);
    }
    
    
    
    int ret = inet_pton(AF_INET, ip, &servaddr.sin_addr);
    if(ret != 1)
        printf("FAILURE: %d\n", ret);
    
    sockfd = Socket(AF_INET, SOCK_DGRAM, 0);
    
    char buf[MAXLINE];
    strcpy(buf, "Hello\n");
    printf("%s", buf);
    Sendto(sockfd, buf, strlen(buf), 0, (SA*)&servaddr, sizeof(servaddr));
}
