#include <stdio.h>
#include "eth0.h"
#include "timer.h"

uint8_t routerMacAddress[6] = {0,0,0,0,0,0};
uint8_t adaFruitIp[4] = {52, 54, 110, 50};
//uint8_t ipFrom[4] = {192, 168, 1, 107};
uint8_t ipTo[4] = {192, 168, 1, 1};
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

void tcpClientSendMessage(etherHeader *ether, uint8_t type) {

    uint8_t mac[6];
    uint8_t i;
    uint32_t sum;

    // Ether frame
    etherGetMacAddress(mac);
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = routerMacAddress[i];
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
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
//        ip->destIp[i] = 0xFF;
        ip->destIp[i] = adaFruitIp[i];
//        ip->sourceIp[i] = 0x0;
    }
    etherGetIpAddress(ip->sourceIp);
//    ip->destIp = {};
    tcpHeader* tcp = (tcpHeader*)ip->data;
    tcp->sourcePort = htons(50000);
    tcp->destPort = htons(1883);
    tcp->sequenceNumber = htonl(random32());
    tcp->acknowledgementNumber = 0;
    // first 4 bits = size of tcp header/4
    // next 3 bits reserved
    // reamining 9 = flag bits = type of message
    tcp->offsetFields = 0;
    tcp->windowSize = htons(500);
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
