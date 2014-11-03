#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>

#include "unpifiplus.h"
#include "shared.h"

static struct sockaddr_in servaddr;

static int finished = 0;
static int recievedEOF = 0;
static Window *window = NULL;
static int lastAck = 0;
static int option;
static int connFd;
static int latestTimeStamp;
static float dropProb;
static int meanReadTime;
static int numConsecTimeoutsForACK = 0;

static int initSocketFd;
static int isLocal;
char * initFilename;
static int  initWindowSize;
static int firstHandshakeTimeoutCount = 0;

#pragma mark - Simple Lock

static int lock = 0;

void block(int myId) {
    while(lock != myId) {
        if(lock == 0)
            lock = myId;
        sleep(0.05);
    }
}

void unlock(int myId) {
    if(lock == myId)
        lock = 0;
}

int checkIfSameNode(char *serverIp, char *clientIp) {
    struct ifi_info *ifi, *ifihead;
    struct sockaddr *sa;
    SocketInfo *socketInfo = NULL;
    u_char      *ptr;
    int     i, family, doaliases;
    family = AF_INET;
    doaliases = 1;
    const int on = 1;
    int found = 0;
    
    printf("Checking if server is local:\n");
    for (ifihead = ifi = get_ifi_info_plus(family, doaliases); ifi != NULL; ifi = ifi->ifi_next) {
        char ip[MAXLINE];
        int listenfd;
        //grabbing the ip adress
        if((sa = ifi->ifi_addr) != NULL) {
            char *tempIp = Sock_ntop_host(sa, sizeof(*sa));
            strcpy(ip, tempIp);
        }
        //extracting the network mask
        char * netMask = NULL;
        if((sa = ifi->ifi_ntmaddr) != NULL)
            netMask = Sock_ntop_host(sa, sizeof(*sa));
        
        //create socket info struct and add to list
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
    
    SocketInfo *si = socketInfo;
    SocketInfo *possibleNonLocal = NULL;
    
    for(si = socketInfo; si != NULL; si = si->next) {
        int ret = strcmp(si->readableIp, serverIp);
        if(ret == 0) {
            strcpy(serverIp, "127.0.0.1");
            strcpy(clientIp, "127.0.0.1");
            printf("Server and Client both set to loop back address\n");
            found = 1;
            break;
        }
        
        if(strcmp("127.0.0.1", si->readableIp) != 0) {
            possibleNonLocal = si;
        }
    }
    
    if(found == 0) {
        if(possibleNonLocal != NULL) {
            strcpy(clientIp, possibleNonLocal->readableIp);
        }
        else {
            printf("there is no ip adress found for client, using 127.0.0.1\n");
        }
    }

    if(checkIfOnSameNetwork(socketInfo, serverIp)) {
        printf("Server is on same network as client\n");
        found = 1;
    }
    else {
        printf("Server and client on different networks\n");
    }
    
    //printf("Debug IPServer: %s\nDebug IPClient: %s\nDebug values\nSee GetSockName below for real IPClient\nSee GetPeerName below for real IPserver\n\n", serverIp, clientIp);
    
    return found;
}

int createSocket(int isLocal, char *ipClient, char *ipServer, int port) {
    const int on = 1;
    struct sockaddr_in addr, addr2;
    int len = sizeof(struct sockaddr);
    char readableIp[MAXLINE];
    
    int listenfd = Socket(AF_INET, SOCK_DGRAM, 0);
    Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if(isLocal) {
        printf("Setting DONTROUTE socket option\n");
        Setsockopt(listenfd, SOL_SOCKET, SO_DONTROUTE, &on, sizeof(on));
    }
    printf("\n");
    
    struct sockaddr_in  cliaddr;
    bzero(&cliaddr, sizeof(cliaddr));
    cliaddr.sin_family      = AF_INET;
    cliaddr.sin_port        = htons(0);
    inet_pton(AF_INET, ipClient, &cliaddr.sin_addr);
    Bind(listenfd, (SA *) &cliaddr, sizeof(cliaddr));
    
    //get sock name
    getsockname(listenfd, (struct sockaddr *)&addr, &len);
    inet_ntop(AF_INET, &addr.sin_addr, readableIp, MAXLINE);
    printf("GetSockName:\nIPClient: %s\nEphemeral Port: %d\n\n", readableIp, ntohs(addr.sin_port));
    
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_port        = htons(port);
    inet_pton(AF_INET, ipServer, &servaddr.sin_addr);
    connect(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    len = sizeof(servaddr);
    
    //get peer name
    getpeername(listenfd, (struct sockaddr*)&addr2, &len);
    inet_ntop(AF_INET, &addr2.sin_addr, readableIp, MAXLINE);
    printf("GetPeerName:\nIPServer: %s\nPort: %d\n\n", readableIp, ntohs(addr2.sin_port));
    
    return listenfd;
}

//sends the filename
void sendFirstHandshake() {
    firstHandshakeTimeoutCount++;
    int socketFd = initSocketFd;
    char *filename = initFilename;
    int windowSize = initWindowSize;

    option = (isLocal == 1)? MSG_DONTROUTE : 0;
    send(socketFd,filename,strlen(filename),option);
  
    char msg[MAXLINE];
    struct sockaddr_in cliaddr;
    socklen_t len = sizeof(cliaddr);

    struct pollfd pollFD;
    int res;
    pollFD.fd = socketFd;
    pollFD.events = POLLIN;
    res = poll(&pollFD, 1, 3000);

    if (res == 0) {
        //timeout
        printf("TIME OUT AFTER SENDING FIRST HANDSHAKE\n");
        if(firstHandshakeTimeoutCount > 5) {
            printf("We have failed to connect to the server more then 5 times\n");
            printf("Aborting...\n");
            exit(1);
        }
        else {
            printf("Attempt %d/5\n", firstHandshakeTimeoutCount);
            printf("Attemping again...\n");
            sendFirstHandshake();
            return;
        }
    }
    else if(res != -1) {
        recvfrom(socketFd, msg, MAXLINE, 0, (SA*)&cliaddr, &len);
        removeNewLine(msg);
        if(strlen(msg) > 0)
            printf("Client recieved second handshake - server port: %s\n", msg);
        
        //send the third handshake
        int servPort = atoi(msg);
        servaddr.sin_port        = htons(servPort);
        connect(socketFd, (struct sockaddr *)&servaddr, sizeof(servaddr));
        printf("sending thirdhandshake to server!! ACK\n");
        char smsg[MAXLINE];
        sprintf(smsg, "ThirdHandshake: %d", windowSize);
        send(socketFd, smsg, strlen(smsg), option);
    }
    else {
        printf("Error in poll, aborting\n");
        exit(1);
    }
}

int shouldDrop() {
    if(((float)rand() / (float)RAND_MAX) < dropProb) 
        return 1;
    else
        return 0;
}

void recieveFile(int socketFd, float dProb, int isLocal) {
    dropProb = dProb;
    int option = (isLocal == 1)? MSG_DONTROUTE : 0;
    char rcvBuf[MAX_PACKET]; 
    char rcvContent[MAX_CONTENT+1];
    char sendingBuf[MAX_PACKET];
    bzero(rcvBuf, sizeof(rcvBuf));
    bzero(rcvContent, sizeof(rcvContent));

    int nrecv;
    char *eof = "EOFEOFEOFEOF";

    int seqNum = 0;
    int isEOF = 0;
    int isProbe = 0;
    int terminate = 0;
    int timestamp = 0;

    connFd = socketFd;

    for(;recievedEOF == 0;) {
        struct pollfd pollFD;
        int res;
        pollFD.fd = socketFd;
        pollFD.events = POLLIN;
        res = poll(&pollFD, 1, 30 * 1000);
        if(res == 0) {
            //timeout
            printf("We have not recieved anything in 30 seonds. Terminating...\n");
            exit(1);
        }
        else if(res != -1) {
            //recv
            if(recievedEOF == 0 && (nrecv = recv(socketFd, rcvBuf, MAX_PACKET, 0)) >= 0) {
                sigset_t *signal_set = malloc(sizeof(sigset_t));
                sigemptyset(signal_set);
                sigaddset(signal_set, SIGALRM);
                sigprocmask(SIG_BLOCK, signal_set, NULL);

                block(4);
                // printf("\n\nRECV: %s\n", rcvBuf);
                bzero(sendingBuf, sizeof(sendingBuf));
                sscanf(rcvBuf, "Header: SeqNum: %d EOF: %d Probe: %d Terminate: %d TS: %d Content: %412c END OF PACKET", &seqNum, &isEOF, &isProbe, &terminate, &timestamp, rcvContent);
                // printf("rcvContent: %s\n", rcvContent);
                printf("Recieved SeqNum: %d ", seqNum);

                if(shouldDrop() == 0) {
                    printf("- producer keeping packet\n");
                    numConsecTimeoutsForACK = 0;
                    int spaceLeft = availWindoSize(window); 
                    latestTimeStamp = timestamp;

                    if(isProbe) {
                        int recvSize = availWindoSize(window);
                        sprintf(sendingBuf, "Header: SeqNum: %d WinSize: %d TS: %d Done: %d", lastAck, recvSize, timestamp, recievedEOF);
                        printf("Client Probed: Sending window update to server: %s\n", sendingBuf);

                        if(shouldDrop() == 0)
                            send(socketFd,sendingBuf,strlen(sendingBuf),option);
                        else 
                            printf("send() dropped for ack: %d\n", lastAck);
                    }
                    else if(spaceLeft > 0) {
                        lastAck = insertPacket(window, rcvContent, seqNum);
                        int recvSize = availWindoSize(window);
                        if(isEOF == 1) {
                            recievedEOF = 1;
                            printf("Recieved EOF from server\n");
                        }

                        sprintf(sendingBuf, "Header: SeqNum: %d WinSize: %d TS: %d Done: %d", lastAck, recvSize, timestamp, recievedEOF);
                        printf("Sending to server: %s\n", sendingBuf);
                        if(shouldDrop() == 0)
                            send(socketFd,sendingBuf,strlen(sendingBuf),option);
                        else
                            printf("send() dropped for ack: %d\n", lastAck);
                    }
                    else {
                        printf("Window is full, packet ignored: %d\n", seqNum);
                    }
                }
                else {
                    printf("- producer dropping packet.\n");    
                }

                bzero(rcvBuf, sizeof(rcvBuf));
                bzero(rcvContent, sizeof(rcvContent));
                sigprocmask(SIG_UNBLOCK, signal_set, NULL);
                free(signal_set);
                unlock(4);
            }
        }
        else {
            //poll error
            printf("Poll error: %d\n", res);
        }
    }

    unlock(4);

    printf("Exiting file transfer\n");

    if(recievedEOF) {
        sleep(5);
    }

    if(nrecv < 0) {
        perror("error in recv, aborting");
        exit(1);
    }
}

#pragma mark - Consumer

static void * runConsumerThread(void *arg) {
    Pthread_detach(pthread_self());
    //FILE *outputFile = fopen("output.txt", "w");

    for(;;) {
        block(2);

        int sizeBeforeRead = availWindoSize(window);
        //readFromWindow(window, window->numberCells, outputFile);
        readFromWindow(window, window->numberCells, NULL);
        printWindow(window);
        int size = availWindoSize(window);
        
        if(size == window->numberCells && recievedEOF == 1) {
            //printf("Closing outputFile\nTerminating Consumer thread");
            //fclose(outputFile);
            finished = 1;
            break;   
        }
        else if(sizeBeforeRead == 0 && size > 0) {
            //send window update
            char sendingBuf[MAX_PACKET]; 
            bzero(sendingBuf, sizeof(sendingBuf));
            sprintf(sendingBuf, "Header: SeqNum: %d WinSize: %d TS: %d Done: %d", lastAck, size, latestTimeStamp, recievedEOF);
            printf("\nConsumer cleaned up full buffer:\nSending window update to server: %s\n", sendingBuf);
            if(shouldDrop() == 0)
                send(connFd,sendingBuf,strlen(sendingBuf),option);
            else
                printf("send() dropped for ack: %d\n", lastAck);
        }
        
        float offset = (((float)rand() / (float)RAND_MAX) - 0.5) * meanReadTime;
        float sleepTimeMS = meanReadTime + offset;
        float sleepTimeS = sleepTimeMS / 1000;
        printf("Consumer sleeping for %f seconds\n", sleepTimeS);
        sleepTimeS = max(sleepTimeS, 1);
        
        unlock(2); 
        sleep(sleepTimeS);
    }

    unlock(2);

    return NULL;
}

void spawnConsumerThread() {
    printf("Spawning consumer thread\n");
    pthread_t tid;
    Pthread_create(&tid, NULL, &runConsumerThread, NULL);
}

#pragma mark - Main

int main(int argc, char **argv) {
    char line[MAXLINE];
    char serverIp[MAXLINE], clientIp[MAXLINE];
    char filename[MAXLINE];
    
    printf("Client.in:\n");
    int fd = open("client.in", O_RDONLY);
    printf("FD: %d\n", fd);
    
    Readline(fd, serverIp, MAXLINE);
    removeNewLine(serverIp);
    printf("ip: %s\n",serverIp);
    
    Readline(fd, line, MAXLINE);
    int port = atoi(line);
    printf("Port: %d\n",port);
    
    Readline(fd, filename, MAXLINE);
    removeNewLine(filename);
    printf("filename: %s\n",filename); 
    
    Readline(fd, line, MAXLINE);
    int windowSize = atoi(line);
    printf("windowSize: %d\n",windowSize);
     
    Readline(fd, line, MAXLINE);
    int seed = atoi(line);
    printf("seed: %d\n",seed);
    srand(seed);
    
    Readline(fd, line, MAXLINE);
    float prob =  (float)strtod(line, NULL);
    printf("prob: %f\n",prob);
    
    
    Readline(fd, line, MAXLINE);
    meanReadTime = atoi(line);
    printf("readingRate: %d\n\n",meanReadTime);
    
    close(fd);
    
    isLocal = checkIfSameNode(serverIp, clientIp);
    int socketFd = createSocket(isLocal, clientIp, serverIp, port);

    initSocketFd = socketFd;
    initFilename = filename;
    initWindowSize = windowSize;

    sendFirstHandshake();
    window = makeWindow(windowSize);
    spawnConsumerThread();
    recieveFile(socketFd, prob, isLocal);

    for(;;) {
        if(finished == 1) {
            printf("File transfer complete, terminating\n\n");
            break;
        }
        else if(finished == -1) {
            break;
        }
        sleep(1);
    }
}
