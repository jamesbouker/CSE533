#ifndef __server_child
#define __server_child

#include "unpifiplus.h"
#include "shared.h"

static uint16_t serverPort;
static int connFd;
static int probeLater = 0;

#define EOFTXT  "EOFEOFEOFEOF"

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

void sendWindowCell(WindowCell *cell, int options, int isEof, Window *window) {
    char toSend[MAX_PACKET];
    bzero(toSend, sizeof(toSend));

    cell->inFlight = 1;
    snprintf(toSend, MAX_PACKET,"Header: SeqNum: %d EOF: %d Probe: %d Terminate: %d Content: %s END OF PACKET", cell->seqNum, isEof, 0,0, cell->data);
    
    printf("\nSending data: %s\n", toSend);
    send(connFd,toSend,strlen(toSend),options);

    printServerWindow(window);
}

int sendMoreData(Window *window, int fd, int options, int cliWinSize) {
    char buff[MAX_CONTENT+1];
    bzero(buff, sizeof(buff));
    int nread;
    int hasData = 1;
    int numberPacketsToSend = min(cliWinSize, numberOpenSendCells(window));
    int i;

    if(numberPacketsToSend > 0)
        printf("About to send %d packets to client\n", numberPacketsToSend);
    else {
        printf("Client Window is full, we need to probe later\n");
        probeLater = 1;
    }

    for(i=0; i<numberPacketsToSend; i++) {
        bzero(buff, sizeof(buff));
        nread = read(fd, buff, MAX_CONTENT);
        if(nread > 0) {
            //we read buff
            printf("\n\nREAD: %d\n", i);
            //printf("buff: %s\n", buff);
            WindowCell *cell = addToWindow(window, buff);
            sendWindowCell(cell, options, 0, window);
        }
        else if(nread == 0) {
            //we read EOF
            printf("\n\nREAD EOF: %d\n", i);
            //printf("buff: %s\n", buff);
            WindowCell *cell = addToWindow(window, EOFTXT);
            sendWindowCell(cell, options, 1, window);
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

void sendTerminate(int options) {
    char toSend[MAX_PACKET];
    bzero(toSend, sizeof(toSend));
    snprintf(toSend, MAX_PACKET,"Header: SeqNum: %d EOF: %d Probe: %d Terminate: %d Content: %s END OF PACKET", 0, 0, 0, 1,"");
    
    printf("Sending Terminate: %s\n", toSend);
    send(connFd,toSend,strlen(toSend),options);

    sleep(5);
}

void sendProbe(int options) {
    char toSend[MAX_PACKET];
    bzero(toSend, sizeof(toSend));
    snprintf(toSend, MAX_PACKET,"Header: SeqNum: %d EOF: %d Probe: %d Terminate: %d Content: %s END OF PACKET", 0, 0, 1, 0, "");
    
    printf("Sending Probe: %s\n", toSend);
    send(connFd,toSend,strlen(toSend),options);
}

void resendWindow(Window *window, int seqNum, int options) {
    int index = oldestCell(window);
    WindowCell *cell = &window->cells[index];
    int isEOF = 0;
    int firstSeqNum = cell->seqNum;

    for(;;) {
        if(cell->inFlight == 1) {
            if(strcmp(EOFTXT, cell->data) == 0)
                isEOF = 1;
            sendWindowCell(cell, options, isEOF, window);

            index++;
            index = index % window->numberCells;
            cell = &window->cells[index]; 
            if(cell->seqNum <= firstSeqNum)
                break;
        }
        else 
            break;
    }
}

void handleDataTransfer(Window * window, int  fd, int options, int hasData) {
    int lastACK = -1;
    int ackCount;
    char rcvBuf[MAX_PACKET];
    int ackNum;
    int cliWinSize;
    int numAcks; 
    int transferDone;

    for(;;) {
        struct pollfd pollFD;
        int res;
        pollFD.fd = connFd;
        pollFD.events = POLLIN;
        res = poll(&pollFD, 1, 3 * 1000); //3 second timeout

        if (res == 0) {
            // timeout
            if(probeLater == 1) {
                probeLater = 0;
                printf("\nTIMEOUT in recv: sending probe\n\n");
                sendProbe(options);
            }
            else {
                printf("\nTIMEOUT in recv: resending window\n\n");
                resendWindow(window, lastACK, options);
            }
        }
        else if (res != -1) {
            if(recv(connFd, rcvBuf, MAX_PACKET, 0) >= 0) {
                printf("Server recieved: %s\n", rcvBuf);
                sscanf(rcvBuf, "Header: SeqNum: %d WinSize: %d Done: %d", &ackNum, &cliWinSize, &transferDone);
                bzero(rcvBuf, sizeof(rcvBuf));

                if(transferDone) {
                    printf("Transfer complete\n");
                    sendTerminate(options);
                    break;
                }
                else if(lastACK < ackNum) {
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
                        resendWindow(window, lastACK, options); 
                        continue;
                    }
                }

                int oldestIndex = oldestCell(window);
                WindowCell *cell = &window->cells[oldestIndex];
                numAcks = ackNum - cell->seqNum;
                int numInFlight = numberOfInFlightPackets(window);

                if(numAcks > 0) {
                    windowRecieved(window, numAcks);
                    printServerWindow(window);
                    if(hasData)
                        hasData = sendMoreData(window, fd, options, cliWinSize - numInFlight);
                }
                else if(numInFlight == 0 && cliWinSize > 0) {
                    //recovering from window lock on probe response
                    if(hasData)
                        hasData = sendMoreData(window, fd, options, cliWinSize );
                }

            }
            else {
                printf("Error in rcv\n");
            }
        }
        else {
            //error in poll
            printf("Error in poll: %d\n", res);
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
