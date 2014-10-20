#ifndef	__shared_h
#define	__shared_h

void removeNewLine(char *str) {
    int len = strlen(str);
    int i;
    for(i=0; i<len; i++) {
        char c = str[i];
        if(c == '\n' || c == '\r')
            str[i] = (char)NULL;
    }
}

typedef struct {
    int sockFd;
    char readableIp[MAXLINE];
    char readableNetwork[MAXLINE];
    char readableSubnet[MAXLINE];
    uint32_t actualIp;
    uint32_t actualNetwork;
    uint32_t actualSubnet;
    void *next;
} SocketInfo;

static SocketInfo* SocketInfoMake(int sockFd, char *ipAddress, char *networkMask) {
    removeNewLine(ipAddress);
    removeNewLine(networkMask);
    
    SocketInfo *si = malloc(sizeof(SocketInfo));
    si->sockFd = sockFd;
    strcpy(si->readableIp, ipAddress);
    strcpy(si->readableNetwork, networkMask);
    
    inet_pton(AF_INET, ipAddress, &si->actualIp);
    inet_pton(AF_INET, networkMask, &si->actualNetwork);
    
    si->actualSubnet = si->actualIp & si->actualNetwork;
    inet_ntop(AF_INET, &si->actualSubnet, si->readableSubnet, MAXLINE);
    
    si->next = NULL;
    printf("Socket Info: \nIp: %s - %u\nNetMast: %s - %u\nSubnet: %s - %u\n\n",ipAddress, si->actualIp, networkMask, si->actualNetwork, si->readableSubnet, si->actualSubnet);
    return si;
}

static SocketInfo* lastSocket(SocketInfo* sock) {
    if(sock == NULL) {
        return NULL;
    }
    
    SocketInfo *ptr = sock;
    while (ptr->next != NULL) {
        ptr = ptr->next;
    }
    return ptr;
}
#endif
