#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "unpifiplus.h"

#pragma mark - Main

extern struct ifi_info *get_ifi_info_plus(int family, int doaliases);
extern        void      free_ifi_info_plus(struct ifi_info *ifihead);

void createListeningSockets(int port) {
    struct ifi_info	*ifi, *ifihead;
    struct sockaddr	*sa;
    u_char		*ptr;
    int		i, family, doaliases;
    
    family = AF_INET;
    doaliases = 1;
    
    const int on = 1;
    int listenfd;
    int fileDescriptors[3];
    int fdIndex = 0;
    
    for (ifihead = ifi = get_ifi_info_plus(family, doaliases); ifi != NULL; ifi = ifi->ifi_next) {
        printf("%s: ", ifi->ifi_name);
        if (ifi->ifi_index != 0)
            printf("(%d) ", ifi->ifi_index);
        printf("<");
        if (ifi->ifi_flags & IFF_UP)			printf("UP ");
        if (ifi->ifi_flags & IFF_BROADCAST)		printf("BCAST ");
        if (ifi->ifi_flags & IFF_MULTICAST)		printf("MCAST ");
        if (ifi->ifi_flags & IFF_LOOPBACK)		printf("LOOP ");
        if (ifi->ifi_flags & IFF_POINTOPOINT)	printf("P2P ");
        printf(">\n");
        
        if ( (i = ifi->ifi_hlen) > 0) {
            ptr = ifi->ifi_haddr;
            do {
                printf("%s%x", (i == ifi->ifi_hlen) ? "  " : ":", *ptr++);
            } while (--i > 0);
            printf("\n");
        }
        if (ifi->ifi_mtu != 0)
            printf("  MTU: %d\n", ifi->ifi_mtu);
        
        if ( (sa = ifi->ifi_addr) != NULL) {
            //make listening socket
            char * ip = Sock_ntop_host(sa, sizeof(*sa));
            printf("  IP addr: %s\n", ip);
            
            listenfd = Socket(AF_INET, SOCK_DGRAM, 0);
            fileDescriptors[fdIndex++] = listenfd;
            
            Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
            struct sockaddr_in	servaddr;
            
            bzero(&servaddr, sizeof(servaddr));
            servaddr.sin_family      = AF_INET;
            
            servaddr.sin_port        = htons(port);
            inet_pton(AF_INET, ip, &servaddr.sin_addr);
            
            Bind(listenfd, (SA *) &servaddr, sizeof(servaddr));
        }
        /*=================== cse 533 Assignment 2 modifications ======================*/
        
        if ( (sa = ifi->ifi_ntmaddr) != NULL)
            printf("  network mask: %s\n", Sock_ntop_host(sa, sizeof(*sa)));
        
        /*=============================================================================*/
        
        if ( (sa = ifi->ifi_brdaddr) != NULL)
            printf("  broadcast addr: %s\n", Sock_ntop_host(sa, sizeof(*sa)));
        if ( (sa = ifi->ifi_dstaddr) != NULL)
            printf("  destination addr: %s\n", Sock_ntop_host(sa, sizeof(*sa)));
    }
    free_ifi_info_plus(ifihead);
    
    //use select to block and listen
    listenfd = fileDescriptors[1];
    char mesg[MAXLINE];
    struct sockaddr_in cliaddr;
    socklen_t len = sizeof(cliaddr);
    Recvfrom(listenfd, mesg, MAXLINE, 0, (SA*)&cliaddr, &len);
    printf("mesg: %s\n", mesg);
}

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

    
    createListeningSockets(port);
    
    close(fd);
    
    exit(0);
}
