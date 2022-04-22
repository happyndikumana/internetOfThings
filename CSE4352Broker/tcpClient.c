#include <stdio.h>
#include <stdbool.h>
#include "tcpClient.h"
#include "eth0.h"
#include "uart0.h"
#include "dhcp.h"
#include "timer.h"

//-----------------------------------------------------------------------------
// TCP Message Type
//-----------------------------------------------------------------------------
#define ETHER_SYN 0x002
#define ETHER_SYN_ACK 0x12
#define ETHER_ACK 0x010
#define ETHER_FIN 0x1
#define ETHER_FIN_ACK 0x11
#define ETHER_PSH_ACK 0x18
//-----------------------------------------------------------------------------
// Ports
//-----------------------------------------------------------------------------
#define SOURCEPORT 52150
#define HTTPPORT 80
#define SERVERPORT 1883

//-----------------------------------------------------------------------------
// TCP States
//-----------------------------------------------------------------------------
#define TCP_CLOSED 1
#define TCP_SYN_SENT 2
#define TCP_ESTABLISHED 3
#define TCP_CLOSE_WAIT 4
#define TCP_LAST_ACK 5
#define TCP_FIN_WAIT_ONE 6
#define TCP_FIN_WAIT_TWO 7
#define TCP_TIME_WAIT 8

//-----------------------------------------------------------------------------
// MQTT Packet types
//-----------------------------------------------------------------------------
#define CONNECT 0x1
#define CONNACK 0x2
#define PUBLISH 0x3
#define DISCONNECT 0xE

//-----------------------------------------------------------------------------
// Other Defines
//-----------------------------------------------------------------------------
#define SIZE 5
//-----------------------------------------------------------------------------
// Globals
//-----------------------------------------------------------------------------
uint8_t tcpState = TCP_CLOSED;
uint8_t google[4] = {142,251,40,196};
uint8_t adaFruitIp[4] = {52, 54, 110, 50};
uint8_t serverIp[4] = {192,168,1,12};
uint8_t serverMacAddress1[6] = {0x14, 0x59, 0xc0, 0x4d, 0x43, 0x80};
uint8_t serverMacAddress2[6] = {0x3c, 0x37, 0x86, 0x2d, 0xb2, 0x3d};
uint8_t happyMacAddress[6] = {0x02, 0x03, 0x04, 0x05, 0x06, 0x6b};
uint8_t routerMacAddress[6] = {0,0,0,0,0,0};
uint8_t ipTo[4] = {192,168,1,1};
clientSocket s[SIZE];
bool mqttConnectFlag = false;
bool mqttDisconnectFlag = false;

extern bool sendMessageRequest;

void tcpSetState(uint8_t state)
{
    tcpState = state;
}

uint8_t tcpGetState()
{
    return tcpState;
}


void getDnsData(etherHeader *ether, uint8_t ipFrom[4]) {
    etherSendArpRequest(ether, ipFrom, ipTo);
    while(1) {
        while(!etherIsDataAvailable()); // && etherIsArpResponse(ether)));
        etherGetPacket(ether, 1522);
        if(etherIsArpResponse(ether))
            break;
    }
    //handle
    arpPacket *arp = (arpPacket*)ether->data;
    uint8_t i;
    for(i = 0; i < 6; i++) {
        routerMacAddress[i] = arp->sourceAddress[i];
    }
}


void extractTcpData(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    uint8_t i = 0;
//  Filling out client socket
    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        s[0].clientIpAddress[i] = ip->sourceIp[i];
        s[0].serverIpAddress[i] = ip->destIp[i];
    }


    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        s[0].clientHwAddress[i] = ether->sourceAddress[i];
        s[0].serverHwAddress[i] = ether->destAddress[i];
    }

    s[0].clientPort = htons(tcp->sourcePort);
    s[0].serverPort = htons(tcp->destPort);
}
void addMqtt(tcpHeader *tcp, uint8_t flag) {
    flag = flag >> 4;
    if(flag == CONNECT) {
        mqttConnect *mqtt = (mqttConnect*)tcp->data;
        mqtt->controlHeader = 0;
        mqtt->controlHeader = mqtt->controlHeader | (flag << 4);

        //sum of variable header and payload
        mqtt->remainingLength = 54;
        mqtt->protocolNameLength = htons(0x0004);

        mqtt->protocolName[0] = 'M';
        mqtt->protocolName[1] = 'Q';
        mqtt->protocolName[2] = 'T';
        mqtt->protocolName[3] = 'T';

        mqtt->protocolLevel = 0x04;
        mqtt->connectFlag = 0xC2;

        mqtt->keepAlive = htons(0x0036);
        mqtt->clientIdLength = htons(0x0000);
    //    mqtt->clientId[0] = 0;
    //    mqtt->clientId[1] = 0;
    //    mqtt->clientId[2] = 0;
    //    mqtt->clientId[3] = 0;
    //    mqtt->clientId[4] = 0;
        mqtt->userNameLength = htons(0x0006);
        mqtt->username[0] = 'm';
        mqtt->username[1] = 'q';
        mqtt->username[2] = 't';
        mqtt->username[3] = 't';
        mqtt->username[4] = '1';
        mqtt->username[5] = '2';

        mqtt->passwordLength = htons(0x0020);
        char tempPassword[32] = "aio_Sigv27mdyLL1F78lB2j4w4IfOISc";
        uint8_t i = 0;
        for(i = 0; i < 32; i++)
        {
            mqtt->password[i] = tempPassword[i];
        }
    }
    if(flag == DISCONNECT) {
        mqttDisconnect *mqtt = (mqttDisconnect*)tcp->data;
        mqtt->controlHeader = 0;
        mqtt->controlHeader = mqtt->controlHeader | (flag << 4);
        mqtt->remainingLength = 0;
    }
}
void tcpSendMessage(etherHeader *ether, bool isFin)
{
    uint8_t mac[6];
    uint8_t i = 0;
    uint32_t sum = 0;
    uint16_t tmp16 = 0;
    etherGetMacAddress(mac);
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->sourceAddress[i] = mac[i];
        ether->destAddress[i] = routerMacAddress[i];
    }
    ether->frameType = htons(0x800);

    ipHeader *ip = (ipHeader*)ether->data;
    ip->revSize = 0x45;
    uint32_t ipLength = (ip->revSize & 0xf) * 4; //getting length of the ip header
    ip->typeOfService = 0;
    ip->id = 0;
    ip->flagsAndOffset = 0;
    ip->ttl = 128;
    ip->protocol = 6;
    ip->headerChecksum = 0;
    etherGetIpAddress(ip->sourceIp);

    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        ip->destIp[i] = adaFruitIp[i];
    }

    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    tcp->sourcePort = htons(SOURCEPORT);
    tcp->destPort = htons(SERVERPORT);
    tcp->offsetFields = 0;

    tcp->windowSize = htons(1024);
    tcp->urgentPointer = 0;
//  store the length of ip
    ip->length = htons(ipLength + sizeof(tcpHeader));
    tcp->offsetFields |= htons(((sizeof(tcpHeader)/4) << 12)); //the upper 4 bits holds the tcp header length

    if(tcpState == TCP_CLOSED)
    {
        tcp->sequenceNumber = htonl(random32());
        s[0].clientSequenceNumber = ntohl(tcp->sequenceNumber); //saving client sequence number
        tcp->acknowledgementNumber = htonl(0);
        tcp->offsetFields |= htons(ETHER_SYN);
        tcpSetState(TCP_SYN_SENT);
    }

    else if(tcpState == TCP_SYN_SENT)
    {
        tcp->sequenceNumber = htonl(s[0].serverAckNumber); //sequence number == the server ack number
        s[0].clientSequenceNumber = ntohl(tcp->sequenceNumber); //updating client seq number in the socket
        tcp->acknowledgementNumber = htonl(s[0].serverSequenceNumber+1); //ack number = server sequence number+1
        tcp->offsetFields |= htons(ETHER_ACK);
        tcpSetState(TCP_ESTABLISHED);
    }

    else if(tcpState == TCP_ESTABLISHED)
    {
        tcp->sequenceNumber = htonl(s[0].serverAckNumber);
        s[0].clientSequenceNumber = ntohl(tcp->sequenceNumber); //updating client seq number in the socket
        tcp->acknowledgementNumber = htonl(s[0].serverSequenceNumber+1);

        if(mqttConnectFlag) {
            tcp->offsetFields |= htons(0x0018);
            addMqtt(tcp, (CONNECT << 4));


            tmp16 += sizeof(mqttConnect);
            mqttConnectFlag = false;
            ip->length = htons(ipLength + sizeof(tcpHeader) + sizeof(mqttConnect));
        }
        else if(mqttDisconnectFlag) {
            tcp->offsetFields |= htons(0x0018);
            addMqtt(tcp, (DISCONNECT << 4));

            tmp16 += sizeof(mqttDisconnect);
            mqttDisconnectFlag = false;
            ip->length = htons(ipLength + sizeof(tcpHeader) + sizeof(mqttDisconnect));
        }
        else if(isFin)
        {
            tcp->offsetFields |= htons(ETHER_FIN_ACK);
            tcpSetState(TCP_FIN_WAIT_ONE);
            isFin = false; //reset flag;
        }
        //TODO change this to account for mqtt otherwise state machine will mess up, also need
        //to store the new socket seq/ack numbers after we process the
        else
        {
            tcp->offsetFields |= htons(ETHER_ACK);
            tcpSetState(TCP_CLOSE_WAIT);
            sendMessageRequest = true;
        }
    }

    else if(tcpState == TCP_CLOSE_WAIT)
    {
        tcp->sequenceNumber = htonl(s[0].serverAckNumber);
        s[0].clientSequenceNumber = ntohl(tcp->sequenceNumber); //updating client seq number in the socket
        tcp->acknowledgementNumber = htonl(s[0].serverSequenceNumber); //no incrementing for FINACK
        tcp->offsetFields |= htons(ETHER_FIN_ACK);
        tcpSetState(TCP_LAST_ACK);
    }

    else if(tcpState == TCP_TIME_WAIT)
    {
        tcp->sequenceNumber = htonl(s[0].serverAckNumber);
        s[0].clientSequenceNumber = ntohl(tcp->sequenceNumber) +1; //updating client seq number in the socket
        tcp->acknowledgementNumber = htonl(s[0].serverSequenceNumber +1); //no incrementing for FINACK
        tcp->offsetFields |= htons(ETHER_ACK);
        tcpSetState(TCP_CLOSED);
    }

    extractTcpData(ether); //extracting data to save for later


    etherCalcIpChecksum(ip);
//  calculating tcp checksum
    etherSumWords(ip->sourceIp, 8, &sum);
//    tmp16 = ip->protocol;
    sum += (ip->protocol & 0xff) << 8;
    tmp16 += sizeof(tcpHeader);
    sum += htons(tmp16);
//    etherSumWords(&tmp16 , 2, &sum);
    tcp->checksum = 0;
    etherSumWords(tcp, tmp16, &sum);
    tcp->checksum = getEtherChecksum(sum);
//  send packet with size = ether + ip + tcp
    etherPutPacket(ether, sizeof(etherHeader) + ipLength + sizeof(tcpHeader) + sizeof(mqttConnect));
}

bool tcpIsSynAck(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    bool ok = false;
    int i = 0;
    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        ok = ip->destIp[i] == s[0].clientIpAddress[i]; //checking if message destination is my ip
        ok = ip->sourceIp[i] == s[0].serverIpAddress[i]; //checking if the source is the server i just contacted
        if(!ok)
            break;
    }

    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ok = ether->destAddress[i] == s[0].clientHwAddress[i]; //checking if mac addresses match
        ok = ether->sourceAddress[i] == s[0].serverHwAddress[i];
        if(!ok)
            break;
    }
    ok = (ntohs(tcp->offsetFields) & 0x01FF) == ETHER_SYN_ACK;
    if(!ok)
        return false;
    ok = ntohs(tcp->sourcePort) == s[0].serverPort; //checking if ports match
    if(!ok)
        return false;
    ok = ntohs(tcp->destPort) == s[0].clientPort;
    if(!ok)
        return false;
    uint32_t temp = ntohl(tcp->acknowledgementNumber);
    ok = temp == s[0].clientSequenceNumber+1; //checking if server ack number is the client sequence number +1
    if(!ok)
        return false;

    if(ok)//  Need to extract sequence/ack number data to socket
    {
        s[0].serverAckNumber = ntohl(tcp->acknowledgementNumber);
        s[0].serverSequenceNumber = ntohl(tcp->sequenceNumber);
    }
    return ok;
}

bool tcpIsFinAck(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    bool ok = false;
    int i = 0;
    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        ok = ip->destIp[i] == s[0].clientIpAddress[i]; //checking if message destination is my ip
        ok = ip->sourceIp[i] == s[0].serverIpAddress[i]; //checking if the source is the server i just contacted
        if(!ok)
            break;
    }

    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ok = ether->destAddress[i] == s[0].clientHwAddress[i]; //checking if mac addresses match
        ok = ether->sourceAddress[i] == s[0].serverHwAddress[i];
        if(!ok)
            break;
    }
    ok = (ntohs(tcp->offsetFields) & 0x01FF) == ETHER_FIN_ACK;
    if(!ok)
        return false;
    ok = ntohs(tcp->sourcePort) == s[0].serverPort; //checking if ports match
    if(!ok)
        return false;
    ok = ntohs(tcp->destPort) == s[0].clientPort;
    if(!ok)
        return false;
    uint32_t temp = ntohl(tcp->acknowledgementNumber);
    if(tcpGetState() == TCP_FIN_WAIT_TWO)
    {
        ok = temp == s[0].serverAckNumber;
    }
    else ok = temp == s[0].clientSequenceNumber; //checking if server ack number is the client sequence number +1
    if(!ok)
        return false;

    if(ok)//  Need to extract sequence/ack number data to socket
    {
        s[0].serverAckNumber = ntohl(tcp->acknowledgementNumber);
        s[0].serverSequenceNumber = ntohl(tcp->sequenceNumber);
    }
    return ok;
}

bool tcpIsAck(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    bool ok = false;
    int i = 0;
    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        ok = ip->destIp[i] == s[0].clientIpAddress[i]; //checking if message destination is my ip
        ok = ip->sourceIp[i] == s[0].serverIpAddress[i]; //checking if the source is the server i just contacted
        if(!ok)
            break;
    }

    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ok = ether->destAddress[i] == s[0].clientHwAddress[i]; //checking if mac addresses match
        ok = ether->sourceAddress[i] == s[0].serverHwAddress[i];
        if(!ok)
            break;
    }

    ok = (ntohs(tcp->offsetFields) & 0x01FF) == ETHER_ACK;
    if(!ok)
        return false;
    ok = ntohs(tcp->sourcePort) == s[0].serverPort; //checking if ports match
    if(!ok)
        return false;
    ok = ntohs(tcp->destPort) == s[0].clientPort;
    if(!ok)
        return false;
    uint32_t temp = ntohl(tcp->acknowledgementNumber);
    ok = temp == s[0].clientSequenceNumber+1; //checking if server ack number is the client sequence number +1
    if(!ok)
        return false;

    if(ok)//  Need to extract sequence/ack number data to socket
    {
        s[0].serverAckNumber = ntohl(tcp->acknowledgementNumber);
        s[0].serverSequenceNumber = ntohl(tcp->sequenceNumber);
    }
    return ok;
}

bool tcpIsMqttConAck(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    mqttConnect *mqtt = (mqttConnect*)tcp->data;
    bool ok = false;
    int i = 0;
    for(i = 0; i < IP_ADD_LENGTH; i++)
    {
        ok = ip->destIp[i] == s[0].clientIpAddress[i]; //checking if message destination is my ip
        ok = ip->sourceIp[i] == s[0].serverIpAddress[i]; //checking if the source is the server i just contacted
        if(!ok)
            break;
    }

    for(i = 0; i < HW_ADD_LENGTH; i++)
    {
        ok = ether->destAddress[i] == s[0].clientHwAddress[i]; //checking if mac addresses match
        ok = ether->sourceAddress[i] == s[0].serverHwAddress[i];
        if(!ok)
            break;
    }

    ok = (ntohs(tcp->offsetFields) & 0x01FF) == ETHER_PSH_ACK;
    if(!ok)
        return false;
    ok = ntohs(tcp->sourcePort) == s[0].serverPort; //checking if ports match
    if(!ok)
        return false;
    ok = ntohs(tcp->destPort) == s[0].clientPort;
    if(!ok)
        return false;

    uint32_t temp = ntohl(tcp->acknowledgementNumber);
    ok = temp == s[0].clientSequenceNumber+sizeof(mqttConnect); //checking if server ack number is the client sequence number +1
    if(!ok)
        return false;

    if(ok)//  Need to extract sequence/ack number data to socket
    {
        s[0].serverAckNumber = ntohl(tcp->acknowledgementNumber);
        s[0].serverSequenceNumber = ntohl(tcp->sequenceNumber);
    }
}

void tcpProcessTcpResponse(etherHeader *ether)
{
    if(tcpIsSynAck(ether))
    {
        sendMessageRequest = true;
    }

    if(tcpIsFinAck(ether))
    {
        if(tcpGetState() == TCP_FIN_WAIT_TWO)
        {
            tcpSetState(TCP_TIME_WAIT);
            sendMessageRequest = true;
        }
        else if(tcpGetState() == TCP_ESTABLISHED)
        {
            sendMessageRequest = true;
        }

    }

    if(tcpIsAck(ether))
    {
        if(tcpGetState() == TCP_FIN_WAIT_ONE)
        {
            tcpSetState(TCP_FIN_WAIT_TWO);
        }

        if(tcpGetState() == TCP_LAST_ACK)
        {
            tcpSetState(TCP_CLOSED);
            s[0].clientSequenceNumber = 0;
            s[0].clientAckNumber = 0;
            s[0].serverSequenceNumber = 0;
            s[0].serverAckNumber = 0;
        }
    }
}



