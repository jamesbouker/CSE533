#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include "unpifiplus.h"
#include "shared.h"
#include "serverChild.h"

extern struct ifi_info *get_ifi_info_plus(int family, int doaliases);
extern        void      free_ifi_info_plus(struct ifi_info *ifihead);

#pragma mark - Children

typedef struct {
    char clientAdr[MAXLINE];
    int process;
} ClientChild;

static ClientChild children[20];

void setup() {
    int i;
    for(i=0; i<20; i++)
        children[i].process = -1;
}

int doesChildExist(char *ip) {
    int i;
    for(i=0; i<20; i++) {
        printf("strcmp(%s, %s): %d\n", ip, children[i].clientAdr, strcmp(ip, children[i].clientAdr));
        if(strcmp(ip, children[i].clientAdr) == 0)
            return i;
    }
    return -1;
}

int insertChild(char *ip, int pId) {
    int i;
    for(i=0; i<20; i++) {
        if(children[i].process == -1) {
            bzero(children[i].clientAdr, sizeof(children[i].clientAdr));
            strcpy(children[i].clientAdr, ip);
            children[i].process = pId;

            printf("Inserted child with ip: %s, process: %d at index: %d\n", children[i].clientAdr, children[i].process, i);
            return i;
        }
    }
    return -1;
}

void removeChild(int index) {
    children[index].process = -1;
    bzero(children[index].clientAdr, sizeof(children[index].clientAdr));
}

void forkChild(char *cliIp, int cliPort, SocketInfo *si, SocketInfo *socketInfo, struct sockaddr_in cliaddr, socklen_t clilen, char* filename, int windowSize) {
    int childpid;
    if((childpid = Fork()) == 0) {
        //child
        handleClient(cliIp, cliPort, si, socketInfo, cliaddr, clilen, filename, windowSize);
        printf("Child exiting\n");
        exit(0);
    }
    else {
        //parent
        insertChild(cliIp, childpid);
    }
}

SocketInfo* createListeningSockets(int port, int windowSize) {
    struct ifi_info *ifi, *ifihead;
    struct sockaddr *sa;
    SocketInfo *socketInfo = NULL;
    
    u_char      *ptr;
    int     i, family, doaliases;
    
    family = AF_INET;
    doaliases = 1;
    
    const int on = 1;
    
    char* filename;
    
    for (ifihead = ifi = get_ifi_info_plus(family, doaliases); ifi != NULL; ifi = ifi->ifi_next) {
        char ip[MAXLINE];
        int listenfd;
        //grabbing the ip adress, and creating a socket
        if((sa = ifi->ifi_addr) != NULL) {
            char *tempIp = Sock_ntop_host(sa, sizeof(*sa));
            strcpy(ip, tempIp);
            
            printf("IP: %s\n", tempIp);
            listenfd = Socket(AF_INET, SOCK_DGRAM, 0);
            Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
            struct sockaddr_in  servaddr;
            bzero(&servaddr, sizeof(servaddr));
            servaddr.sin_family      = AF_INET;
            servaddr.sin_port        = htons(port);
            inet_pton(AF_INET, tempIp, &servaddr.sin_addr);
            Bind(listenfd, (SA *) &servaddr, sizeof(servaddr));
        }
        
        //extracting the network mask
        char * netMask = NULL;
        if((sa = ifi->ifi_ntmaddr) != NULL)
            netMask = Sock_ntop_host(sa, sizeof(*sa));
        
        //listening socket record keeping
        if(ip!=NULL && netMask != NULL) {
            SocketInfo *si = SocketInfoMake(listenfd, ip, netMask);
            if(socketInfo == NULL)
                socketInfo = si;
            else {
                SocketInfo *last = lastSocket(socketInfo);
                last->next = si;
            }
        }
    }
    free_ifi_info_plus(ifihead);
    
    for(;;) {
        //use select to block and listen
        fd_set lset;
        FD_ZERO(&lset);
        
        SocketInfo *si = socketInfo;
        for(si = socketInfo; si != NULL; si = si->next) {
            FD_SET(si->sockFd,&lset);
        }
        
        //use select to monitor
        int sel = 0;
        sel = Select(FD_SETSIZE, &lset, NULL, NULL, NULL);
        if(sel > 0) {
            for(si = socketInfo; si != NULL; si = si->next) {
                if(FD_ISSET(si->sockFd, &lset)) {
                    char mesg[MAXLINE];
                    struct sockaddr_in cliaddr;
                    socklen_t clilen = sizeof(cliaddr);
                    recvfrom(si->sockFd, mesg, MAXLINE, 0, (SA*)&cliaddr, &clilen);
                    int cliPort = ntohs(cliaddr.sin_port);
                    char cliIp[MAXLINE];
                    inet_ntop(AF_INET, &cliaddr.sin_addr, cliIp, MAXLINE);
                    removeNewLine(cliIp);
                    printf("msg recieved: %s\nOn: %s\nClient Port: %d\nClient IP: %s\n\n", mesg, si->readableIp, cliPort, cliIp);
                    
                    filename = mesg;
                    printf("Server has file: %s\n", filename);
                    
                    int childIndex = doesChildExist(cliIp);
                    printf("ChildIndex: %d\n", childIndex);
                    if(childIndex == -1) {
                        forkChild(cliIp, cliPort, si, socketInfo, cliaddr, clilen, filename, windowSize);
                    }
                    else {
                        printf("Recieved first handshake again - killing child and re creating new one for handle\n");
                        ClientChild child = children[childIndex];
                        kill(child.process,SIGKILL);
                        removeChild(childIndex);

                        forkChild(cliIp, cliPort, si, socketInfo, cliaddr, clilen, filename, windowSize);
                    }
                }
            }
        }
    }
    
    return socketInfo;
}

int main(int argc, char **argv) {
    setup();
    char line[MAXLINE];
    
    int fd = open("server.in", O_RDONLY);
    printf("FD: %d\n", fd);
    
    Readline(fd, line, MAXLINE);
    int port = atoi(line);
    printf("Port: %d\n",port);
    
    Readline(fd, line, MAXLINE);
    int windowSize = atoi(line);
    printf("windowSize: %d\n\n",windowSize);
    
    
    createListeningSockets(port, windowSize);
    
    close(fd);
    
    exit(0);
}
