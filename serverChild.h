#ifndef __server_child
#define __server_child

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
    
    struct sockaddr_in  servaddr;
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
    
    struct sockaddr_in  cliaddr;
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
    printf("Server sent second handshake: %s to: %d\n", serverPortString, socketFd);
}

int waitForThirdHandshake() {
    char mesg[MAXLINE];
    struct sockaddr_in cliaddr;
    socklen_t clilen = sizeof(cliaddr);
    recvfrom(connFd, mesg, MAXLINE, 0, (SA*)&cliaddr, &clilen);
    printf("Recieved 3rd handshake: %s\n", mesg);
    int cliWinSize;
    sscanf(mesg, "ThirdHandshake: %d", &cliWinSize);
    printf("cliWinSize: %d\n", cliWinSize);
    return cliWinSize;
}

void sendWindowCell(WindowCell *cell, int options) {
    cell->inFlight = 1;
    printf("Sending Cell: %d\nData: %s\n",cell->seqNum, cell->data);
    send(connFd,cell->data,strlen(cell->data),options);
}

int sendMoreData(Window *window, int fd, int options, int cliWinSize) {
    printf("in sendMoreData\n");
    char buff[MAXLINE];
    bzero(buff, sizeof(buff));
    int nread;
    int hasData = 1;
    int numberPacketsToSend = min(cliWinSize, numberOpenSendCells(window));
    int i;
    printf("numberPacketsToSend: %d\n", numberPacketsToSend);

    for(i=0; i<numberPacketsToSend; i++) {
        bzero(buff, sizeof(buff));
        nread = read(fd, buff, MAXLINE);
        if(nread > 0) {
            //we read buff
            printf("\n\nREAD: %d\n", i);
            //printf("buff: %s\n", buff);
            WindowCell *cell = addToWindow(window, buff);
            sendWindowCell(cell, options);
        }
        else if(nread == 0) {
            //we read EOF
            printf("\n\nREAD EOF: %d\n", i);
            //printf("buff: %s\n", buff);
            WindowCell *cell = addToWindow(window, "EOFEOFEOFEOF");
            sendWindowCell(cell, options);
            hasData = 0;
            break;
        }
        else if(nread < 0) {
            perror("error reading from file, aborting...\n");
            exit(1);
        }
    }

    if(hasData == 0) {
        if(close(fd) < 0)
            perror("file close failed!\n");
        else
            printf("closed file\n");
    }

    return hasData;
}

void handleDataTransfer(Window * window, int  fd, int options, int hasData) {
    int lastACK = -1;
    int ackCount;
    char rcvBuf[MAXLINE];
    int ackNum;
    int cliWinSize;
    int numAcks;

    for(;;) {
        if(recv(connFd, rcvBuf, MAXLINE, 0) >= 0) {
            printf("Server recieved an %s\n", rcvBuf);
            sscanf(rcvBuf, "ACK:%d WinSize:%d", &ackNum, &cliWinSize);
            bzero(rcvBuf, sizeof(rcvBuf));

            if(lastACK < ackNum) {
                lastACK = ackNum;
                ackCount = 0;
                printf("Now waiting on ACK: %d\n", lastACK+1);
            }
            else {
                ackCount++;
                printf("Still waiting on ACK: %d\n", lastACK+1);
                if(ackCount >= 3) {
                    printf("Recieved 3 other ACKS while waiting on ACK: %d\n", lastACK+1);
                    printf("Time to resend!\n\n");
                    //todo: call resendWindow()
                    //keep the continue here!
                    continue;
                }
            }

            int oldestIndex = oldestCell(window);
            WindowCell *cell = &window->cells[oldestIndex];
            numAcks = ackNum - cell->seqNum;

            if(numAcks > 0) {
                windowRecieved(window, numAcks);
                printRcvWindow(window);
                if(hasData)
                    hasData = sendMoreData(window, fd, options, cliWinSize);
                else {
                    //we have pushed all data to our window
                    if(numberOfInFlightPackets(window) == 0) {
                        //we are done
                        break;
                    }
                }
            }

        }
        else {
            printf("Error in rcv\n");
        }
    }
}

int setupWindow(Window *window, int fd, int initialRcvWinSize, int options) {
    return sendMoreData(window, fd, options, initialRcvWinSize);
}

void sendFile(char *filename, int options, int windowSize, int cliWinSize) {
    Window *window = makeWindow(windowSize);
    int myfd = open(filename, O_RDONLY);
    int nread;
    int hasData;

    if(myfd < 0)
        perror("file open failed!\n");
    else
        printf("we are in business...\n");

    hasData = setupWindow(window, myfd, cliWinSize, options);
    handleDataTransfer(window, myfd, options, hasData);
}

void handleClient(char *ipClient, int cliPort, SocketInfo *listening, SocketInfo *root, struct sockaddr_in cliaddr, socklen_t clilen, char* filename, int windowSize) {
    printf("Child process forked\n");
    int options = 0;
    int cliWinSize;

    closeSockets(listening, root);
    
    if(checkIfClientLocal(root, ipClient))
        options = MSG_DONTROUTE;
    
    createConnectionSocket(listening, ipClient, cliPort, options);
    sendSecondHandshake(listening->sockFd, options, serverPort, cliaddr, clilen);
    cliWinSize =  waitForThirdHandshake();
    
    printf("Closing original listening socket\n");
    close(listening->sockFd);
    
    
    sendFile(filename, options, windowSize, cliWinSize);
}

#endif
