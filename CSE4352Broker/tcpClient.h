//tcp header file

#ifndef TCPCLIENT_H_
#define TCPCLIENT_H_

#include <stdint.h>
#include <stdbool.h>
#include "eth0.h"

typedef struct _clientSocket
{
    uint8_t clientIpAddress[4];
    uint8_t clientHwAddress[6];
    uint16_t clientPort;
    uint32_t clientSequenceNumber;
    uint32_t clientAckNumber;

    uint8_t serverIpAddress[4];
    uint8_t serverHwAddress[6];
    uint16_t serverPort;
    uint32_t serverSequenceNumber;
    uint32_t serverAckNumber;
}clientSocket;
typedef struct _mqttConnect // 35 bytes
{
    uint8_t controlHeader;
    uint8_t remainingLength;
    uint16_t protocolNameLength;
    uint8_t protocolName[4];
    uint8_t protocolLevel;
    uint8_t connectFlag;
    uint16_t keepAlive;
    uint16_t clientIdLength;
//    uint8_t clientId[5];
    uint16_t userNameLength;
    uint8_t username[6];
    uint16_t passwordLength;
    uint8_t password[32];
}mqttConnect;
typedef struct _mqttDisconnect
{
    uint8_t controlHeader;
    uint8_t remainingLength;
}mqttDisconnect;

extern bool mqttConnectFlag;
extern bool mqttDisconnectFlag;


//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void addMqtt(tcpHeader *tcp, uint8_t flag);
void tcpSendMessage(etherHeader *ether, bool isFin);
void tcpProcessTcpResponse(etherHeader *ether);
void getDnsData(etherHeader *ether, uint8_t ipFrom[4]);

#endif
