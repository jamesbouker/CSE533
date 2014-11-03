Team Members : 
James Bouker  - 107091895
Yasin Zaidi   - 107762771 

Implemented:
  Flow control: Reciever window
  Congestion control: Slow start, Fast Recovery, Congestion Avoidance
  Reliable data transfer: using sequence numbers, acks, timeouts

ADRESS BINDING

  We iterate through all interfaces of the machine using get_ifi_info_plus and store them in a link list defined in shared.h

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

  Server: Iterates through the list creating listening sockets for each SocketInfo
  Client: Also uses SocketInfo to determine location in networkd of the server 

  Local Vs Non Local: This is done by first building the structure and linked list mentioned above
    We then iterate through the list finding the greatest prefexi match of the subnet adress and ip adress we are testing.
    

    Note: We convert ip's from integer to binary string (110101010101000101010) 
    using a function takend from this stack over flow post
      http://stackoverflow.com/questions/699968/display-the-binary-representation-of-a-number-in-c
    **This is the only code we have taken from the internet

RELIABLE UDP

  Every packet sent out is assigned a SeqNum. Recievers acknowledge the packet with an ACK back to the sender.
  The Sender also has a timeout for any given packet, if the timeout is triggered it resends the last packet the client has asked for in it's ACK.

CONGESTION CONTROL

  Emplemented using cwn and ssthresh along with Slow Start and Congestion Avoidance

  Stages:
    Slow Start: while cwnd < ssthresh increment cwind on receiving an ACK
      ssthresh on start is ignored until we recieve our very first error (timeout or duplicate ack)
    Congestion Avoidance: while cwind > ssthresh
      only increment cwin if we recieve ACKS back for n packets where n = cwin

  Special Events:
    Timeout: ssthresh = cwin/2 and cwin = 1 
      Causes server to enter Slow Start, or CA if both cwin and ssthresh are 1
    3 Dup ACKS: ssthresh = cwind/2 and cwin = cwin / 2
      Causes server to enter Congestion Avoidance
    when updating ssthresh and cwin take a max(1, v) where v is either cwin or ssthresh appropriatley
    We do not want either to be equal to 0

  Number Segments in flight: we never want to over send the clients recieving window
  When reciving an ACK we check the clients available window size. 
  We take the min of (cliWinSize, cwin, serverWinSize, numDataLeft) and subtract from the answer the number of in flight packets

RTT MODIFACTIONS
  We modifed the rtt.h file given in stevens book.

  All calculations are now done with integers. We are also representing the timestamp time as microseconds.
  Note to store this in a 32 bit integer we subtract the base time which is calculated on start.

  implemented using rtt_start(), rtt_stop(), rtt_timeout(), rtt_ts()
  rtt_ts()      returns the current timestamp minus the current rtt_base (so it can fit in an int)
  rtt_start()   will return the time needed to wait until firing a timeout event
  rtt_stop()    is called when recieveing a packet to inform the rtt mechanism of the RTT
  rtt_timeout() is called to inform the rtt mechanism of a timeout, it then doubles the rto value

FLOW CONTROL

  Both thr Server and Client make use of ARQ sliding windows. Both client and window contain and update a circular array aka the Window

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

  The server and client differ in slighty in how they use it:
    Server: fills in the window first, setting data to the txt to send. As the server sends window cells it sets the inFlight field to 1 and moves it's ptr. When an ACK is heard, it finds the cell with the lowest seqNum and removes from there up to the ACK #. It can then read in more info and fill the window accordingly, setting the new seqNum's and inFlight values appropritaley.
    
    Client: fills the window as it recieves data from the server, thus marking each cell as arrived and copying the data into the cells data buffer. When the consumer is ready to read, it iterates from the lowest seqNum to highest printing out the contents of data, and setting the new seqNum and arrived fields appropritaley

  CLIENT ACKS: Every client ACK contains the recieving window size so the server knows how many packets to send

  Avoiding Deadlock: If the client responds with a cliWinSize of 0, the server will stop sending until hearing an ACK back informing it otherwise.
  But the client's ACKS may be lost. To prevent deadlock the Server sends a probe message every time he timesout. The Client responds to the probe with the last ACK # he has sent out as well as his current windowSize. 

  - The client also informs the server when the consumer reads and frees up the window

END OF FILE and Termination

  When the server sends the final packet, he marks EOF: 1 in the header. This the client can now terminate.
  But before doing so he attemps to kill the server by sending one last ACK and waiting for a few seconds. 

  Both client and server have ways of terminating using timeout mechanisms if this final packet is lost or if the connection is lost.
