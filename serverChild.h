#ifndef __server_child
#define __server_child

#include "unpifiplus.h"
#include "shared.h"
#include "rtt.h"

static uint16_t serverPort;
static int connFd;
static int probeLater = 0;
static int eofACK = -1;

typedef enum {
    CStateSlowStart = 0,
    CStateCAvoidance,
    CStateFastRecovery
} CState;
 
static CState congestionState = CStateSlowStart;
static int cwin = 1;
static int ssthresh = -1;
static int numberACKSWithoutTimeouts = 0;
static rtt_info rttInfo;

#define EOFTXT  "EOFEOFEOFEOF"

int checkIfClientSameNode(SocketInfo *root, char *ipClient) {
    SocketInfo *inf;
    for(inf = root; inf != NULL; inf = inf->next) {
        if(strcmp(ipClient, inf->readableIp) == 0) {
            printf("Client IP is on same node: using MSG_DONTROUTE\n");
            return 1;
        }
    }
    printf("Client IP is not on this node: %s\n", ipClient);
    return 0;
}

void closeSockets(SocketInfo *listening, SocketInfo *root) {
    SocketInfo *inf;
    printf("Closing all sockets except listening socket\n");
    for(inf = root; inf != NULL; inf = inf->next) {
        if(listening->actualIp != inf->actualIp) {
            close(inf->sockFd);
        }
    }
}

void createConnectionSocket(SocketInfo *listening, char *ipClient, int cliPort, int options) {
    int on = 1;
    struct sockaddr_in addr, addr2;
    int len = sizeof(struct sockaddr);
    char readableIp[MAXLINE];
    
    printf("\n\nCreating socket:\n");
    
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
    printf("\nGetSockName:\nIPServer: %s\nEphemeral Port: %d\n\n", readableIp, ntohs(addr.sin_port));
    
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
    printf("Server sent second handshake: %s\n", serverPortString);
}

int waitForThirdHandshake() {
    char mesg[MAXLINE];
    struct sockaddr_in cliaddr;
    socklen_t clilen = sizeof(cliaddr);
    recvfrom(connFd, mesg, MAXLINE, 0, (SA*)&cliaddr, &clilen);
    printf("Recieved 3rd handshake: ");
    int cliWinSize;
    sscanf(mesg, "ThirdHandshake: %d", &cliWinSize);
    printf("cliWinSize: %d\n", cliWinSize);
    return cliWinSize;
}

void sendWindowCell(WindowCell *cell, int options, int isEof, Window *window) {
    char toSend[MAX_PACKET];
    bzero(toSend, sizeof(toSend));

    cell->inFlight = 1;
    int ts = rtt_ts(&rttInfo);
    snprintf(toSend, MAX_PACKET,"Header: SeqNum: %d EOF: %d Probe: %d Terminate: %d TS: %d Content: %s END OF PACKET", cell->seqNum, isEof, 0,0, ts, cell->data);
    if(isEof) {
        eofACK = cell->seqNum;
    }

    printf("\nSending Packet: SeqNum: %d, EOF: %d, Probe: %d, TS: %d\n", cell->seqNum, isEof, 0, ts);
    send(connFd,toSend,strlen(toSend),options);

    printServerWindow(window, "sending packet");
}

int sendMoreData(Window *window, int fd, int options, int cliWinSize) {
    char buff[MAX_CONTENT+1];
    bzero(buff, sizeof(buff));
    int nread;
    int hasData = 1;
    int numberPacketsToSend = min(cliWinSize, numberOpenSendCells(window));
    numberPacketsToSend = min(numberPacketsToSend, cwin - numberOfInFlightPackets(window));
    int i;

    if(numberPacketsToSend > 0)
        printf("About to send %d packet(s) to client\n", numberPacketsToSend);
    else {
        printf("Client Window is full, will probe later\n");
        probeLater = 1;
    }

    for(i=0; i<numberPacketsToSend; i++) {
        bzero(buff, sizeof(buff));

        WindowCell *possibleNextInline = getOldestWithDataNotInFlight(window);

        if(possibleNextInline != NULL) {
            printf("resending a lost packet - SeqNum: %d\n", possibleNextInline->seqNum);
            int isEOF = (strcmp(EOFTXT, possibleNextInline->data) == 0) ? 1 : 0;
            sendWindowCell(possibleNextInline, options, isEOF, window);
            if(isEOF)
                hasData = 0;
        }
        else {
            nread = read(fd, buff, MAX_CONTENT);
            if(nread > 0) {
                WindowCell *cell = addToWindow(window, buff);
                sendWindowCell(cell, options, 0, window);
            }
            else if(nread == 0) {
                //we read EOF
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
    }

    if(hasData == 0) {
        if(close(fd) < 0)
            perror("file close failed!\n");
        else
            printf("closed file\n");
    }

    return hasData;
}

void sendProbe(int options) {
    char toSend[MAX_PACKET];
    bzero(toSend, sizeof(toSend));
    snprintf(toSend, MAX_PACKET,"Header: SeqNum: %d EOF: %d Probe: %d Terminate: %d TS: %d Content: %s END OF PACKET", 0, 0, 1, 0, 0, "");
    
    printf("Sending Probe: %s\n", toSend);
    send(connFd,toSend,strlen(toSend),options);
}

void resendWindow(Window *window, int seqNum, int options) {
    int index = oldestCell(window);
    WindowCell *cell = &window->cells[index];
    int isEOF = 0;
    int firstSeqNum = cell->seqNum;
    int numSent = 0;

    for(;;) {
        isEOF = 0;
        if(cell->inFlight == 1) {
            if(strcmp(EOFTXT, cell->data) == 0)
                isEOF = 1;

            if(numSent < cwin) {
                printf("Resending a lost packet - seqNum: %d\n", cell->seqNum);
                sendWindowCell(cell, options, isEOF, window);
                numSent++;
            }
            else {
                cell->inFlight = 0;
            }

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
    int transferDone = 0;
    int timestamp;

    while(transferDone == 0) {
        struct pollfd pollFD;
        int res;
        pollFD.fd = connFd;
        pollFD.events = POLLIN;
        int timeToWait = rtt_start(&rttInfo);
        printf("wait time from rtt: %d milliseconds\n\n", timeToWait);
        res = poll(&pollFD, 1, timeToWait);

        if (res == 0) {
            //if(probeLater == 1) {
                //probe timer
                probeLater = 0;
                printf("\nsending probe\n\n");
                sendProbe(options);
            //}
            //else {
                // timeout
                if(rtt_timeout(&rttInfo) < 0) {
                    printf("Terminating Transaction: rtt_timeout returned -1\n");
                    break;
                }

                if(ssthresh == -1)
                    ssthresh = cwin;

                cwin = 1;
                ssthresh = ssthresh / 2;
                ssthresh = max(1, ssthresh);
                if(cwin != ssthresh) {
                    printf("\nTimeout occurred: cwin = 1 ssthresh = %d\nEntering slow start\n", ssthresh);
                    congestionState = CStateSlowStart;
                }
                else {
                    printf("\nTimeout occurred: cwin = 1 ssthresh = %d\nEntering congestion avoidance\n", ssthresh);
                    congestionState = CStateCAvoidance;
                }
                resendWindow(window, lastACK, options);
            //}
        }
        else if (res != -1) {
            if(recv(connFd, rcvBuf, MAX_PACKET, 0) >= 0) {
                sscanf(rcvBuf, "Header: SeqNum: %d WinSize: %d TS: %d Done: %d", &ackNum, &cliWinSize, &timestamp, &transferDone);
                printf("Server recieved ACK: %d, CliWinSize: %d\n", ackNum, cliWinSize);
                bzero(rcvBuf, sizeof(rcvBuf));

                if(transferDone && eofACK < ackNum) {
                    rtt_stop(&rttInfo, rtt_ts(&rttInfo) - timestamp);
                    printf("Transfer complete\n");
                    break;
                }
                else if(lastACK < ackNum) {
                    lastACK = ackNum;
                    ackCount = 0;
                    rtt_stop(&rttInfo, rtt_ts(&rttInfo) - timestamp);
                    rtt_newpack(&rttInfo);
                    if(congestionState == CStateSlowStart) {
                        if(cwin < ssthresh || ssthresh == -1) { 
                            if(cwin < window->numberCells) { 
                                cwin++;
                                printf("In Slow Start, Cwin increased to: %d\n", cwin);
                            }
                            else
                                printf("In Slow Start, cwin has reached max window size: %d\n", cwin);

                            if(ssthresh ==-1)
                                printf("Haven't encountered first error yet, ssthresh: %d\n",cwin);
                            else
                                printf("ssthresh = %d\n",ssthresh);
                            
                            if(cwin >= ssthresh && ssthresh != -1) {
                                ssthresh = cwin;
                                congestionState = CStateCAvoidance;
                                printf("ssthresh = cwin: Entering Congestion Avoidance state\n");
                                numberACKSWithoutTimeouts = 0;
                            }
                        }
                    }
                    else if(congestionState == CStateCAvoidance) {
                        numberACKSWithoutTimeouts++;
                        printf("In Congestion Avoidance, number of ACKS recieved w/out error: %d\n", numberACKSWithoutTimeouts);
                        if(numberACKSWithoutTimeouts >= cwin) {
                            if(cwin < window->numberCells) {
                                cwin++;
                                ssthresh = cwin;
                                numberACKSWithoutTimeouts = 0;
                                printf("Increasing cwin and ssthresh\nBoth now at %d\n", cwin);
                            }
                            else
                                printf("Would increase cwin but have reached max window size\n");
                        }
                        else {
                            printf("Both cwin and ssthresh = %d\n", cwin);
                        }
                    }
                }
                else {
                    ackCount++;
                    printf("Still waiting on ACK: %d\n", lastACK+1);
                    if(ackCount > 3) {
                        printf("Recieved more than 3 ACKS while waiting on ACK: %d\n", lastACK+1);
                        printf("Fast Recovery occurring\n\n");
                        ackCount = 0;
                        ssthresh = cwin / 2;
                        ssthresh = max(1, ssthresh);
                        cwin = ssthresh;
                        congestionState = CStateCAvoidance;
                        printf("cwin halved to %d\n",cwin);
                        printf("ssthresh set to cwin %d\n",ssthresh);
                        printf("Entering Congestion Avoidance\n\n");
                        resendWindow(window, lastACK, options); 
                        continue;
                    }
                }

                int oldestIndex = oldestCell(window); 
                WindowCell *cell = &window->cells[oldestIndex];
                numAcks = ackNum - cell->seqNum;
                int numInFlight = numberOfInFlightPackets(window);
                WindowCell *oldestNotInFlight = getOldestWithDataNotInFlight(window);

                if(numAcks > 0) {
                    windowRecieved(window, numAcks);
                    printServerWindow(window, "recieving packet");
                    if(hasData || oldestNotInFlight != NULL)
                        hasData = sendMoreData(window, fd, options, cliWinSize - numInFlight);
                }
                else if(numInFlight == 0 && cliWinSize > 0) {
                    //recovering from window lock on probe response
                    if(hasData || oldestNotInFlight != NULL)
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
    congestionState = CStateSlowStart;
    cwin = 1;
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
        printf("file open succeeded\n");

    rtt_init(&rttInfo);
    rtt_newpack(&rttInfo);

    hasData = setupWindow(window, myfd, cliWinSize, options);
    handleDataTransfer(window, myfd, options, hasData);
}

void handleClient(char *ipClient, int cliPort, SocketInfo *listening, SocketInfo *root, struct sockaddr_in cliaddr, socklen_t clilen, char* filename, int windowSize) {
    printf("Child process forked\n");
    int options = 0;
    int cliWinSize;

    closeSockets(listening, root);
    
    if(checkIfClientSameNode(root, ipClient))
        options = MSG_DONTROUTE;
    else if(checkIfOnSameNetwork(root, ipClient)) {
        printf("Client and server on the same network: using MSG_DONTROUTE\n");
        options = MSG_DONTROUTE;
    }
    else {
        printf("Client is on a different network\n");
    }
    
    createConnectionSocket(listening, ipClient, cliPort, options);
    sendSecondHandshake(listening->sockFd, options, serverPort, cliaddr, clilen);
    cliWinSize =  waitForThirdHandshake();
    
    printf("Closing original listening socket\n");
    close(listening->sockFd);
    
    
    sendFile(filename, options, windowSize, cliWinSize);
}

#endif
