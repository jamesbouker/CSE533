#ifndef __shared_h
#define __shared_h

void removeNewLine(char *str) {
    int len = strlen(str);
    int i;
    for(i=0; i<len; i++) {
        char c = str[i];
        if(c == '\n' || c == '\r')
            str[i] = (char)NULL;
    }
}

#pragma mark - Window Cell

typedef struct {
    int arrived;
    int inFlight;
    int seqNum;
    char data[MAXLINE];
} WindowCell;

typedef struct {
    int numberCells;
    WindowCell *cells;
    WindowCell *ptr;
} Window;

Window * makeWindow(int maxCells) {
    Window *window = malloc(sizeof(Window));
    window->numberCells = maxCells;
    window->cells = malloc(sizeof(WindowCell) * maxCells);
    window->ptr = window->cells;

    int i;
    for(i=0; i<maxCells; i++) {
        window->cells[i].seqNum = i;
        window->cells[i].inFlight = 0;
        window->cells[i].arrived = 0;
    }

    return window;
}

int indexOfCell(Window *window, WindowCell *cell) {
    int i; 
    WindowCell cellAtI;
    for(i=0; i<window->numberCells; i++) {
        cellAtI = window->cells[i];
        if(cellAtI.seqNum == cell->seqNum) {
            return i;
        }
    }
    return -1;
}

WindowCell *cellForSeqNum(Window *window, int seqNum) {
    int i;
    for(i=0; i<window->numberCells; i++) {
        if(window->cells[i].seqNum == seqNum) {
            return &window->cells[i];
        }
    }
    return NULL;
}

int availWindoSize(Window *window) {
    int availSize = 0;
    int firstSeqNum = window->ptr->seqNum;
    WindowCell *ptr = window->ptr;
    int index = indexOfCell(window, ptr);
    int windowSize = 0;

    //for a full buffer
    if(ptr->arrived) 
        return 0;

    do {
        windowSize++;
        index++;
        index = index % window->numberCells;
        ptr = &window->cells[index];
    } while (ptr->seqNum > firstSeqNum);

    return windowSize;
}

//used by server
void printRcvWindow(Window *window) {
    int size = numberOpenSendCells(window);
    printf("Window Stats:\nSize: %d/%d free\n", size, window->numberCells);
    int i;
    printf("SeqNum: \t");
    for(i=0; i < window->numberCells; i++)
        printf("|%d", window->cells[i].seqNum);
    printf("\nInFlight:\t");
    for(i=0; i < window->numberCells; i++)
        printf("|%d", window->cells[i].inFlight);
    printf("\n\n");
}

//used by client
void printWindow(Window *window) {
    int size = availWindoSize(window);
    printf("Window Stats:\nSize: %d/%d free\n", size, window->numberCells);
    int i;
    printf("SeqNum: \t");
    for(i=0; i < window->numberCells; i++)
        printf("|%d", window->cells[i].seqNum);
    printf("\nArrived:\t");
    for(i=0; i < window->numberCells; i++)
        printf("|%d", window->cells[i].arrived);
    printf("\n\n");
}

//user by server
WindowCell * addToWindow(Window *window, char *data) {
    strcpy(window->ptr->data, data);

    int firstSeqNum = window->ptr->seqNum;
    WindowCell *ptr = window->ptr;
    int index = indexOfCell(window, ptr);
    WindowCell *lastPtr = ptr;

    index++;
    index = index % window->numberCells;
    ptr = &(window->cells[index]);

    window->ptr = ptr;
    return lastPtr;
}

//used by server
int oldestCell(Window *window) {
    int index = 0;
    int prevSeqNum = window->cells[0].seqNum;
    int currentSeqNum;

    for(;;) {
        index++;
        index = index % window->numberCells;
        currentSeqNum = window->cells[index].seqNum;
        if(currentSeqNum < prevSeqNum)
            return index;
        prevSeqNum = currentSeqNum;
    }
}

//used by server
int youngestCell(Window *window) {
    int index = 0;
    int prevSeqNum = window->cells[0].seqNum;
    int currentSeqNum;
    int prevIndex = 0;

    for(;;) {
        index++;
        index = index % window->numberCells;
        currentSeqNum = window->cells[index].seqNum;
        if(currentSeqNum < prevSeqNum) {
            return prevIndex;
        }
        prevSeqNum = currentSeqNum;
        prevIndex = index;
    }
}

//used by server
void windowRecieved(Window* window, int numberPackets) {
    //set pcakets to new seqNums and set inFlight = 0
    int index = oldestCell(window);
    int i;

    int youngIndx = youngestCell(window);
    WindowCell *youngster = &window->cells[youngIndx];

    for(i=0; i<numberPackets; i++) {
        WindowCell *cell = &window->cells[index+i];
        cell->inFlight = 0;
        cell->seqNum = youngster->seqNum+i+1;
        bzero(cell->data, sizeof(cell->data));
        index++;
    }
}

int numberOfInFlightPackets(Window *window) {
    int i;
    int count = 0;
    for(i=0; i<window->numberCells; i++) {
        if(window->cells[i].inFlight)
            count++;
    }
    return count;
}

//used by server
int numberOpenSendCells(Window *window) {
    int firstSeqNum = window->ptr->seqNum;
    WindowCell *ptr = window->ptr;
    int index = indexOfCell(window, ptr);
    int size = 0;

    for(;;) {
        if(ptr->inFlight == 0) {
            size++;
            index++;
            index = index % window->numberCells;
            ptr = &window->cells[index];

            if(ptr->seqNum <= firstSeqNum)
                break;
        }
        else
            break;
    }

    return size;
}

//used by client
void insertPacket(Window *window, char *msg, int seqNum) {
    //give data to cell
    printf("Insert Packet: %s\n", msg);

    WindowCell *cell = cellForSeqNum(window, seqNum);
    if(cell != NULL) {
        cell->arrived = 1;
        strcpy(cell->data, msg);
    }

    int firstSeqNum = window->ptr->seqNum;
    WindowCell *ptr = window->ptr;
    WindowCell *lastPtr = ptr;
    int index = indexOfCell(window, ptr);

    if(seqNum != firstSeqNum) {
        printf("\nPacket arrived for seqNum: %d\nStill expecting seqNum: %d\n\n", seqNum, lastPtr->seqNum);
    }
    else {
        for(;;) {
            index++;
            index = index % window->numberCells;
            ptr = &(window->cells[index]);
            if(ptr->seqNum > firstSeqNum) {
                lastPtr = ptr;
                if(ptr->arrived == 0)
                    break;
            }
            else
                break;
        }

        printf("\nPacket arrived for seqNum: %d\nNext expected seqNum: %d\n\n", seqNum, lastPtr->seqNum);
        window->ptr = lastPtr;
    }

    printWindow(window);
}

#pragma mark - Socket Info

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
