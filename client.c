#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "unpifiplus.h"
#include "shared.h"

static struct sockaddr_in servaddr;

static int finished = 0;
static int recievedEOF = 0;
static Window *window = NULL;
static int lastAck = 0;
static int option;
static int connFd;

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

int checkIfLocal(char *serverIp, char *clientIp) {
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
        printf("Server is not local\n");
        if(possibleNonLocal != NULL) {
            strcpy(clientIp, possibleNonLocal->readableIp);
        }
        else {
            printf("there is no ip adress found for client, using 127.0.0.1\n");
        }
    }
    
    printf("Debug IPServer: %s\nDebug IPClient: %s\nDebug values\nSee GetSockName below for real IPClient\nSee GetPeerName below for real IPserver\n\n", serverIp, clientIp);
    
    return found;
}

int createSocket(int isLocal, char *ipClient, char *ipServer, int port) {
    const int on = 1;
    struct sockaddr_in addr, addr2;
    int len = sizeof(struct sockaddr);
    char readableIp[MAXLINE];
    
    printf("Creating socket:\n");
    
    int listenfd = Socket(AF_INET, SOCK_DGRAM, 0);
    Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if(isLocal) {
        printf("Set DONTROUTE socket option\n");
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
void sendFirstHandshake(int socketFd, int isLocal, char *filename, int windowSize) {
    option = (isLocal == 1)? MSG_DONTROUTE : 0;
    send(socketFd,filename,strlen(filename),option);
  
    char msg[MAXLINE];
    struct sockaddr_in cliaddr;
    socklen_t len = sizeof(cliaddr);
    recvfrom(socketFd, msg, MAXLINE, 0, (SA*)&cliaddr, &len);
    removeNewLine(msg);
    printf("Client recieved second handshake - server port: %s\n", msg);
    
    //send the third ACK
    int servPort = atoi(msg);
    servaddr.sin_port        = htons(servPort);
    connect(socketFd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    printf("sending thirdhandshake to server!! ACK\n");
    char smsg[MAXLINE];
    sprintf(smsg, "ThirdHandshake: %d", windowSize);
    send(socketFd, smsg, strlen(smsg), option);
}

void recieveFile(int socketFd, float dropProb, int isLocal) {
    int option = (isLocal == 1)? MSG_DONTROUTE : 0;
    char rcvBuf[MAX_PACKET]; 
    char rcvContent[MAX_CONTENT];
    char sendingBuf[MAX_PACKET];
    bzero(rcvBuf, sizeof(rcvBuf));
    bzero(rcvContent, sizeof(rcvContent));

    int nrecv;
    char *eof = "EOFEOFEOFEOF";

    int seqNum = 0;
    int isEOF = 0;
    int isProbe = 0;

    connFd = socketFd;

    while((nrecv = recv(socketFd, rcvBuf, MAX_PACKET, 0)) >= 0) {
        block(4);
        printf("recv: %s\n", rcvBuf);
        bzero(sendingBuf, sizeof(sendingBuf));

        //check if packet lost
        if(((rand() % 100) / 100.0) > dropProb) {
            int spaceLeft = availWindoSize(window);
            sscanf(rcvBuf, "Header: SeqNum: %d EOF: %d Probe: %d Content: %s", &seqNum, &isEOF, &isProbe, rcvContent);

            if(isProbe) {
                int recvSize = availWindoSize(window);
                sprintf(sendingBuf, "Header: SeqNum: %d WinSize: %d", lastAck, recvSize);
                printf("Sending window update to server: %s\n", sendingBuf);
                send(socketFd,sendingBuf,strlen(sendingBuf),option);
            }
            else if(spaceLeft > 0) {
                lastAck = insertPacket(window, rcvBuf, seqNum);
                int recvSize = availWindoSize(window);
                sprintf(sendingBuf, "Header: SeqNum: %d WinSize: %d", lastAck, recvSize);
                printf("Sending to server: %s\n", sendingBuf);
                send(socketFd,sendingBuf,strlen(sendingBuf),option);

                if(isEOF == 1) {
                    recievedEOF = 1;
                    printf("Recieved EOF from server\n");
                    break;
                }
            }
            else {
                printf("Window is full, packet ignored: %d\n", seqNum);
            }
        }
        else {
            printf("producer dropped packet: %d\n", seqNum);    
        }

        bzero(rcvBuf, sizeof(rcvBuf));
        bzero(rcvContent, sizeof(rcvContent));

        unlock(4);
        printf("Main thread unlocked the window\n");
    }

    printf("Exiting the while loop\n");
    unlock(4);
    printf("Main thread unlocked the window\n");

    if(nrecv < 0) {
        perror("error receiving across network!\n");
        exit(1);
    }
}

#pragma mark - Consumer

static void * runConsumerThread(void *arg) {
    Pthread_detach(pthread_self());
    for(;;) {
        block(2);

        int sizeBeforeRead = availWindoSize(window);
        readFromWindow(window, 5);
        printWindow(window);
        int size = availWindoSize(window);
        
        if(size == window->numberCells && recievedEOF == 1) {
            break;   
        }
        else if(sizeBeforeRead == 0 && size > 0) {
            //send window update
            char sendingBuf[MAX_PACKET]; 
            bzero(sendingBuf, sizeof(sendingBuf));
            sprintf(sendingBuf, "Header: SeqNum: %d WinSize: %d", lastAck, size);
            printf("Sending window update to server: %s\n", sendingBuf);
            send(connFd,sendingBuf,strlen(sendingBuf),option);
        }
        
        unlock(2);
        sleep(1);
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
    int readingRate = atoi(line);
    printf("readingRate: %d\n\n",readingRate);
    
    close(fd);
    
    int isLocal = checkIfLocal(serverIp, clientIp);
    int socketFd = createSocket(isLocal, clientIp, serverIp, port);

    sendFirstHandshake(socketFd, isLocal, filename, windowSize);
    window = makeWindow(windowSize);
    spawnConsumerThread();
    recieveFile(socketFd, prob, isLocal);

    for(;;) {
        if(finished)
            break;
        sleep(1);
    }
}
