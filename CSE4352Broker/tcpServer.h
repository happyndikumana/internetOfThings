#ifndef TCPSERVER_H_
#define TCPSERVER_H_

typedef struct _socket // 240 or more bytes
{
    uint8_t occupied;
    uint8_t sourceIpAddress[4];
    uint8_t sourceMacAddress[6];
    uint8_t destIpAddress[4];
    uint8_t destMacAddress[6];
    uint32_t sequenceNumber;
    uint32_t acknowledgeNumber;
    uint16_t destPort;
    uint16_t sourcePort;
    uint16_t state;
    uint16_t previousState;
}socket;

extern bool dataInSocketFlag;

void clientSocketSetToUnoccupied(uint8_t index);
void displayTcpClients();
void tcpEndConnectionCommand(uint8_t octate);
void tcpServerSendMessage(etherHeader *ether, uint8_t type, uint8_t index);
//void listenForEther(etherHeader *ether);
void processTcpServerMessage(etherHeader *ether);
bool extractTcpServerData(etherHeader *ether);
uint8_t getCurrentClientState(uint8_t index);
void setClientState(uint8_t index, uint16_t state, uint16_t previousState);
void sendTcpPendingMessage(etherHeader *ether);
void tcpGetClientMacAddress(uint8_t address[6], uint8_t index);
void tcpGetClientIpAddress(uint8_t address[4], uint8_t index);
uint16_t tcpGetDestPort(uint8_t index);
uint32_t tcpGetClientSeqNumber(uint8_t index);
uint8_t tcpClientGetState(uint8_t index);
uint32_t tcpClientGetAckNumber(uint8_t index);

#endif
