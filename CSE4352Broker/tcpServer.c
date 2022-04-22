#include <stdio.h>
#include "eth0.h"
#include "timer.h"
#include "tcpServer.h"
#include "eth0.h"
#include "wait.h"
#include "uart0.h"

#define TCP_SYN     0x02
#define TCP_SYN_ACK 0x12
#define TCP_ACK     0x10
#define TCP_FIN_ACK 0x11
#define TCP_FIN     0x01

#define TCP_LISTEN       0
#define TCP_SYN_RECEIVED 1

#define TCP_CLOSED 0
#define TCP_SYN_SENT 1
#define TCP_CLIENT_ESTABLISHED 2
#define TCP_CLIENT_FIN_WAIT_1 3
#define TCP_CLIENT_FIN_WAIT_2 4
#define TCP_CLIENT_TIME_WAIT 5
#define TCP_CLIENT_CLOSE_WAIT 6
#define TCP_CLIENT_LAST_ACK 7

#define SOCKETSIZE 100

//uint8_t clientIpAddress[4];
//uint8_t clientMacAddress[6];
//uint32_t clientSeqNumber;
uint8_t currentTcpServerstate;

socket clientSocket[SOCKETSIZE];
bool dataInSocketFlag = false;
bool sendFinFlag = false;
uint8_t globalIndex;

void displayTcpClients() {
    uint8_t i;
     char buffer[20];
    for(i = 0; i < SOCKETSIZE; i++) {
        if(clientSocket[i].occupied == 1) {
            sprintf(buffer,"Mac: %d.%d.%d.%d.%d.%d\n",
                    clientSocket[i].sourceMacAddress[0],
                    clientSocket[i].sourceMacAddress[1],
                    clientSocket[i].sourceMacAddress[2],
                    clientSocket[i].sourceMacAddress[3],
                    clientSocket[i].sourceMacAddress[4],
                    clientSocket[i].sourceMacAddress[5]);
            putsUart0(buffer);
            sprintf(buffer, "Ip: %d,%d,%d,%d \n",
                    clientSocket[i].sourceIpAddress[0],
                    clientSocket[i].sourceIpAddress[1],
                    clientSocket[i].sourceIpAddress[2],
                    clientSocket[i].sourceIpAddress[3]);
            putsUart0(buffer);
        }
    }
}

void tcpEndConnectionCommand(uint8_t octate) {
    uint8_t index = octate % 100;
    if(clientSocket[index].occupied == 0) {
        putsUart0("no client of entered octate.\n");
        return;
    }
    //end the connection for the passed in octate
    putsUart0("ending client.\n");
    //set a flag
    globalIndex = index;
    sendFinFlag = true;
    dataInSocketFlag = true;
}

void setClientState(uint8_t index, uint16_t state, uint16_t previousState) {
    clientSocket[index].state = state;
    clientSocket[index].previousState = previousState;
}
uint8_t getCurrentClientState(uint8_t index) {
    return clientSocket[index].state;
}

//void setTcpServerState(etherHeader *ether){
//    uint8_t state = getTcpServerState();
//    switch(state) {
//        case TCP_LISTEN:
//            break;
//    }
//}
void clientSocketSetToUnoccupied(uint8_t index) {
    clientSocket[index].occupied = 0;
}
uint8_t getSocketIndex(etherHeader *ether) {
    return ether->sourceAddress[5]%100;
}
void sendTcpPendingMessage(etherHeader *ether) {
    uint8_t i;
    for(i = 0; i < SOCKETSIZE; i++) {
        if(clientSocket[i].occupied == 1) {
            uint16_t state = getCurrentClientState(i);
            if(state == TCP_SYN_SENT) {
                tcpServerSendMessage(ether, TCP_SYN_ACK, i);
                dataInSocketFlag = false;
            }
            else if(state == TCP_CLIENT_FIN_WAIT_1) {
                tcpServerSendMessage(ether, TCP_ACK, i);
                setClientState(i, TCP_CLIENT_FIN_WAIT_2, TCP_CLIENT_FIN_WAIT_1);
                tcpServerSendMessage(ether, TCP_FIN_ACK, i);
                setClientState(i, TCP_CLIENT_TIME_WAIT, TCP_CLIENT_FIN_WAIT_2);
            }
            else if(state == TCP_CLIENT_FIN_WAIT_2) {
                tcpServerSendMessage(ether, TCP_FIN_ACK, i);
            }
//            else if(state == TCP_CLIENT_LAST_ACK) {
//                tcpServerSendMessage(ether, TCP_ACK, i);
//                setClientState(i, TCP_CLOSED, TCP_CLOSED);
//                clientSocketSetToUnoccupied(i);
//            }
            else if(state == TCP_CLIENT_ESTABLISHED && sendFinFlag && globalIndex == i) {
                tcpServerSendMessage(ether, TCP_FIN_ACK, i);
                sendFinFlag = false;
//                setClientState(i, TCP_CLIENT_CLOSE_WAIT, TCP_CLIENT_ESTABLISHED);
            }
            else if(state == TCP_CLIENT_LAST_ACK) {
                tcpServerSendMessage(ether, TCP_ACK, i);
                setClientState(i, TCP_CLOSED, TCP_CLIENT_LAST_ACK);
            }
        }
    }
}
void processTcpServerMessage(etherHeader *ether) {
    ipHeader* ip = (ipHeader*)ether->data;
    tcpHeader* tcp = (tcpHeader*)ip->data;
    extractTcpServerData(ether);
}

bool extractTcpServerData(etherHeader *ether) {

    uint8_t i;
    uint8_t myMacAddress[6];
    uint8_t myIpAddress[4];
    ipHeader* ip = (ipHeader*)ether->data;

    bool ok = false;
    etherGetMacAddress(myMacAddress);
    for(i = 0; i < 5; i++) {
        ok = ether->destAddress[i] == myMacAddress[i];
        if(!ok)
            break;
    }
    if(ok) {
        etherGetIpAddress(myIpAddress);
        for(i = 0; i < 4; i++) {
            ok = ip->destIp[i] == myIpAddress[i];
            if(!ok)
                break;
        }
    }
    if(ok) {
        if(!dataInSocketFlag)
            dataInSocketFlag = true;
        tcpHeader* tcp = (tcpHeader*)ip->data;
        uint8_t index = ether->sourceAddress[5]%100;
        clientSocket[index].occupied = 1;
        for(i = 0; i < IP_ADD_LENGTH; i++) {
            clientSocket[index].sourceIpAddress[i] = ip->sourceIp[i];
            clientSocket[index].destIpAddress[i] = ip->destIp[i];
        }
        for(i = 0; i < HW_ADD_LENGTH; i++) {
            clientSocket[index].sourceMacAddress[i] = ether->sourceAddress[i];
            clientSocket[index].destMacAddress[i] = ether->destAddress[i];
        }
        clientSocket[index].sequenceNumber = tcp->sequenceNumber;
        clientSocket[index].acknowledgeNumber = tcp->acknowledgementNumber;
        clientSocket[index].destPort = tcp->destPort;
        clientSocket[index].sourcePort = tcp->sourcePort;
        uint16_t flags = htons(tcp->offsetFields) & 0x01FF;
        if(flags == TCP_SYN) {
            clientSocket[index].previousState = TCP_CLOSED;
            clientSocket[index].state = TCP_SYN_SENT;
        }
        else if(flags == TCP_ACK && clientSocket[index].state == TCP_SYN_SENT) {
            clientSocket[index].previousState = TCP_SYN_SENT;
            clientSocket[index].state = TCP_CLIENT_ESTABLISHED;
        }
        else if(flags == TCP_FIN_ACK && clientSocket[index].state == TCP_CLIENT_ESTABLISHED) {
            clientSocket[index].previousState = TCP_CLIENT_ESTABLISHED;
            clientSocket[index].state = TCP_CLIENT_FIN_WAIT_1;
        }
        else if(flags == TCP_ACK && clientSocket[index].state == TCP_CLIENT_TIME_WAIT) {
            clientSocket[index].previousState = TCP_CLIENT_TIME_WAIT;
            clientSocket[index].state = TCP_CLOSED;
        }
        else if(flags == TCP_ACK && clientSocket[index].state == TCP_CLIENT_ESTABLISHED){
            clientSocket[index].previousState = TCP_CLIENT_ESTABLISHED;
            clientSocket[index].state = TCP_CLIENT_CLOSE_WAIT;
        }
        else if(flags == TCP_FIN_ACK && clientSocket[index].state == TCP_CLIENT_CLOSE_WAIT) {
            clientSocket[index].previousState = TCP_CLIENT_CLOSE_WAIT;
            clientSocket[index].state = TCP_CLIENT_LAST_ACK;
        }
        else {
            if(clientSocket[index].previousState == TCP_CLIENT_FIN_WAIT_2)
                clientSocket[index].previousState = TCP_CLIENT_TIME_WAIT;
            else if(clientSocket[index].previousState == TCP_CLIENT_CLOSE_WAIT)
                clientSocket[index].previousState = TCP_CLIENT_LAST_ACK;
            else
                clientSocket[index].previousState = TCP_CLOSED;
            clientSocket[index].state = TCP_CLOSED;
        }
    }
    return ok;
}

void tcpGetClientMacAddress(uint8_t address[6], uint8_t index) {
    uint8_t i;
    for(i = 0; i < HW_ADD_LENGTH; i++) {
        address[i] = clientSocket[index].sourceMacAddress[i];
    }
}
void tcpGetClientIpAddress(uint8_t address[4], uint8_t index) {
    uint8_t i;
    for(i = 0; i < IP_ADD_LENGTH; i++) {
        address[i] = clientSocket[index].sourceIpAddress[i];
    }
}
uint16_t tcpGetDestPort(uint8_t index) {
    return clientSocket[index].sourcePort;
}
uint16_t tcpGetSourcePort(uint8_t index) {
    return clientSocket[index].destPort;
}
uint32_t tcpGetClientSeqNumber(uint8_t index) {
    return clientSocket[index].sequenceNumber;
}
uint8_t tcpClientGetState(uint8_t index) {
    return clientSocket[index].state;
}
uint32_t tcpClientGetAckNumber(uint8_t index) {
    return clientSocket[index].acknowledgeNumber;
}
void tcpServerSendMessage(etherHeader *ether, uint8_t type, uint8_t index) {
    uint8_t mac[6];
    uint8_t clientMacAddress[4];
    uint8_t clientIpAddress[4];
    uint32_t clientSeqNumber;
    uint8_t i;
    uint32_t sum;

    // Ether frame
    etherGetMacAddress(mac);
    tcpGetClientMacAddress(clientMacAddress, index);
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        //pull it out of the recived tcp message
        ether->destAddress[i] = clientMacAddress[i];
        ether->sourceAddress[i] = mac[i];
    }
    ether->frameType = htons(0x800);

    // IP header
    ipHeader* ip = (ipHeader*)ether->data;
    ip->revSize = 0x45;
    uint8_t ipHeaderLength = (ip->revSize & 0xF) * 4;
    ip->typeOfService = 0;
    ip->id = 0;
    ip->flagsAndOffset = 0;
    ip->ttl = 128;
    ip->protocol = 6;
    ip->headerChecksum = 0;
    tcpGetClientIpAddress(clientIpAddress, index);
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        //client's ip address. pull it out of received tcp message
        ip->destIp[i] = clientIpAddress[i];
    }
    etherGetIpAddress(ip->sourceIp);
    tcpHeader* tcp = (tcpHeader*)ip->data;
    tcp->sourcePort = tcpGetSourcePort(index);
    tcp->destPort = tcpGetDestPort(index);
    clientSeqNumber = htonl(tcpGetClientSeqNumber(index));
    uint16_t currentState = tcpClientGetState(index);
    if(currentState == TCP_CLIENT_ESTABLISHED) {
        tcp->sequenceNumber = tcpClientGetAckNumber(index);
        tcp->acknowledgementNumber = tcpGetClientSeqNumber(index);
    }
    else if(currentState == TCP_CLIENT_LAST_ACK) {
        tcp->sequenceNumber = htonl(htonl(tcpClientGetAckNumber(index)) + 1);
        tcp->acknowledgementNumber = htonl(clientSeqNumber + 1);
    }
    else if(currentState == TCP_CLIENT_FIN_WAIT_1) {
        tcp->sequenceNumber = tcpClientGetAckNumber(index);
        tcp->acknowledgementNumber = htonl(clientSeqNumber + 1);
    }
    else if(currentState == TCP_CLIENT_FIN_WAIT_2) {
        tcp->sequenceNumber = tcpClientGetAckNumber(index);
        tcp->acknowledgementNumber = htonl(htonl(tcpGetClientSeqNumber(index)) + 1);
    }
    else {
        tcp->sequenceNumber = htonl(random32());
        tcp->acknowledgementNumber = htonl(clientSeqNumber + 1);
    }
    //pull out client's sequence number and add 1

    // first 4 bits = size of tcp header/4
    // next 3 bits reserved
    // reamining 9 = flag bits = type of message
    tcp->offsetFields = 0;
    tcp->windowSize = htons(1024);
    tcp->checksum = 0;
    tcp->urgentPointer = 0;
    uint8_t tcpSize = sizeof(tcpHeader)/4;
    // 1101
    // 0000 0000 0000 0000
    uint16_t flags = 0;
    flags |= (tcpSize << 12) | type;
    tcp->offsetFields = htons(flags);

    uint16_t ipSize = ipHeaderLength + sizeof(tcpHeader);
    ip->length = htons(ipSize);
    etherCalcIpChecksum(ip);
    //tcp checksum
    sum = 0;
    //pseudo header
    etherSumWords(ip->sourceIp, 4, &sum);
    etherSumWords(ip->destIp, 4, &sum);
    uint16_t protocolTemp = ip->protocol;
    sum += (protocolTemp & 0xFF) << 8;
    sum += htons(sizeof(tcpHeader));
    //tcp header check sum
    etherSumWords(tcp, sizeof(tcpHeader), &sum);
    tcp->checksum = getEtherChecksum(sum);
    etherPutPacket(ether, sizeof(etherHeader) + ipHeaderLength + sizeof(tcpHeader));
}
