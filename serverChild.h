#ifndef	__server_child
#define	__server_child

#include "unpifiplus.h"
#include "shared.h"

static uint16_t serverPort;
static int connFd;

int checkIfClientLocal(SocketInfo *root, char *ipClient) {
    SocketInfo *inf;
    for(inf = root; inf != NULL; inf = inf->next) {
        if(strcmp(ipClient, inf->readableIp) == 0) {
            printf("Client IP is local: using MSG_DONTROUTE\n");
            return 1;
        }
    }
    printf("Client IP is not local: %s\n", ipClient);
    return 0;
}

void closeSockets(SocketInfo *listening, SocketInfo *root) {
    SocketInfo *inf;
    int closed = 0;
    printf("Closing Sockets: ");
    for(inf = root; inf != NULL; inf = inf->next) {
        if(listening->actualIp != inf->actualIp) {
            close(inf->sockFd);
            if(!closed) {
                closed = 1;
                printf("%d", inf->sockFd);
            }
            else
                printf(", %d", inf->sockFd);
        }
    }
    printf("\n");
}

void createConnectionSocket(SocketInfo *listening, char *ipClient, int cliPort, int options) {
    const int on = 1;
    struct sockaddr_in addr, addr2;
    int len = sizeof(struct sockaddr);
    char readableIp[MAXLINE];
    
    printf("Creating socket:\n");
    
    int listenfd = Socket(AF_INET, SOCK_DGRAM, 0);
    Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if(options) {
        printf("Set DONTROUTE socket option\n");
        Setsockopt(listenfd, SOL_SOCKET, SO_DONTROUTE, &on, sizeof(on));
    }
    
    struct sockaddr_in	servaddr;
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_port        = htons(0);
    inet_pton(AF_INET, listening->readableIp, &servaddr.sin_addr);
    Bind(listenfd, (SA *) &servaddr, sizeof(servaddr));
    
    //get sock name
    getsockname(listenfd, (struct sockaddr *)&addr, &len);
    inet_ntop(AF_INET, &addr.sin_addr, readableIp, MAXLINE);
    serverPort = ntohs(addr.sin_port);
    printf("GetSockName:\nIPServer: %s\nEphemeral Port: %d\n\n", readableIp, ntohs(addr.sin_port));
    
    struct sockaddr_in	cliaddr;
    bzero(&cliaddr, sizeof(cliaddr));
    cliaddr.sin_family      = AF_INET;
    cliaddr.sin_port        = htons(cliPort);
    inet_pton(AF_INET, ipClient, &cliaddr.sin_addr);
    connect(listenfd, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
    
    connFd = listenfd;
}

void intToString(int num, char *buff) {
    if(snprintf(buff, MAXLINE, "%d\n", num) == -1) {
        printf("SHOULD NOT HAPPEN");
    }
}

//sending the port
void sendSecondHandshake(int socketFd, int options, int serverPort, struct sockaddr_in cliaddr, socklen_t clilen) {
    char serverPortString [MAXLINE];
    intToString(serverPort, serverPortString);
    sendto(socketFd,serverPortString,strlen(serverPortString),options, (struct sockaddr *)&cliaddr, clilen);
    printf("Server sent second handshake: %sto: %d\n", serverPortString, socketFd);
}

void waitForThirdHandshake() {
    char mesg[MAXLINE];
    struct sockaddr_in cliaddr;
    socklen_t clilen = sizeof(cliaddr);
    recvfrom(connFd, mesg, MAXLINE, 0, (SA*)&cliaddr, &clilen);
    printf("Recieved 3rd handshake: %s\n", mesg);
}

void handleClient(char *ipClient, int cliPort, SocketInfo *listening, SocketInfo *root, struct sockaddr_in cliaddr, socklen_t clilen) {
    printf("Child process forked\n");
    int options = 0;
    
    closeSockets(listening, root);
    
    if(checkIfClientLocal(root, ipClient))
        options = MSG_DONTROUTE;
    
    createConnectionSocket(listening, ipClient, cliPort, options);
    sendSecondHandshake(listening->sockFd, options, serverPort, cliaddr, clilen);
    waitForThirdHandshake();
    
    printf("Closing original listening socket\n");
    close(listening->sockFd);
}

#endif
