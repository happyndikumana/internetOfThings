// DHCP Library
// Jason Losh and Happy Ndikumana

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: -
// Target uC:       -
// System Clock:    -

// Hardware configuration:
// -

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdio.h>
#include "dhcp.h"
#include "timer.h"
#include "eth0.h"
#include "eeprom.h"

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPDECLINE  4
#define DHCPACK      5
#define DHCPNAK      6
#define DHCPRELEASE  7
#define DHCPINFORM   8
#define DHCPENDOPTION 255

#define DHCP_DISABLED   0
#define DHCP_INIT       1
#define DHCP_SELECTING  2
#define DHCP_REQUESTING 3
#define DHCP_TESTING_IP 4
#define DHCP_BOUND      5
#define DHCP_RENEWING   6
#define DHCP_REBINDING  7
#define DHCP_INITREBOOT 8 // not used since ip not stored over reboot
#define DHCP_REBOOTING  9 // not used since ip not stored over reboot
#define DHCP_RELEASING  10
#define DHCP_REBINDTOINIT 11
#define DHCP_OFF        12

#define SET   1
#define UNSET 0

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

uint8_t dhcpState = DHCP_DISABLED;
uint32_t globalXid;
uint8_t ipOfferedAdd[4];
uint8_t serverIdentifierAdd[4];
uint8_t subnetMaskAdd[4];
uint8_t gateWayAdd[4];
uint8_t domainNameServerAdd[4];
uint8_t ipTimeServerAdd[4];
uint32_t leaseTime;
uint32_t t1LeaseTime;
uint32_t t2LeaseTime;
uint8_t offerTimerFlag = SET;
uint8_t addressesFlag = UNSET;
uint8_t restartLeaseTimerFlag = SET;
uint8_t renewTimerFlag = UNSET;
uint8_t rebindTimerFlag = UNSET;

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// State functions

void dhcpSetState(uint8_t state)
{
    dhcpState = state;
}

uint8_t dhcpGetState()
{
    return dhcpState;
}

// Send DHCP message
void dhcpSendMessage(etherHeader *ether, uint8_t type)
{
    uint32_t sum;
    uint8_t i, opt;
    uint16_t tmp16;
    uint8_t mac[6];
    /*
    uint8_t serverIdentifier[4];
    if(type == DHCPREQUEST) {
        uint8_t optionsLength;
        uint8_t * serverId = getOption(ether, 54, &optionsLength);
        uint8_t h = 0;
        for(h = 0; h < 4; h++) {
            serverIdentifier[h] = serverId[h];
        }
    }
    */

    // Ether frame
    etherGetMacAddress(mac);
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = 0xFF;
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
    ip->protocol = 17;
    ip->headerChecksum = 0;
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        ip->destIp[i] = 0xFF;
        ip->sourceIp[i] = 0x0;
    }

    // UDP header
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    udp->sourcePort = htons(68);
    udp->destPort = htons(67);

    // DHCP
    dhcpFrame* dhcp = (dhcpFrame*)udp->data;
    dhcp->op = 1;
    //
    dhcp->htype = 1;
    dhcp->hlen = 6;
    dhcp->hops = 0;
    //this microcontroller is big endian and the host is little endian -> htonl()
    //the last
    if(type == DHCPDISCOVER)
        globalXid = random32(); //78 47 48 -> 48 47 78
    dhcp->xid = htonl(globalXid);
    dhcp->secs = 0;
    dhcp->flags = htons(0x8000);
    if(type == DHCPREQUEST && dhcpGetState() == DHCP_RENEWING)
        dhcp->flags = 0;
    //dhcp->ciaddr is filled in only in the states: bound, renew, re-binding
    uint8_t j = 0;
    for(j = 0; j < 4; j++) {
        if(dhcpGetState() == DHCP_RENEWING || dhcpGetState() == DHCP_REBINDING)
            dhcp->ciaddr[j] = ipOfferedAdd[j];
        else
            dhcp->ciaddr[j] = 0;
//        if(type == DHCPDISCOVER)
        dhcp->yiaddr[j] = 0;
        dhcp->siaddr[j] = 0;
        dhcp->giaddr[j] = 0;
    }
    if(type == DHCPDISCOVER) {
        for(j = 0; j < 4; j++) {
            dhcp->yiaddr[j] = ipOfferedAdd[j];
        }
    }
    uint8_t macAddress[6];
    etherGetMacAddress(macAddress);
    for(j = 0; j < 6; j++)
        dhcp->chaddr[j] = macAddress[j];
    for(j = 6; j < 16; j++)
        dhcp->chaddr[j] = 0;
    //request state
    for(j = 0; j < 192; j++)
        dhcp->data[j] = 0;
    //
    dhcp->magicCookie = htonl(0x63825363);
    i = 0;
    dhcp->options[i++] = 53;
    dhcp->options[i++] = 1;
    dhcp->options[i++] = type;

    if(type == DHCPDISCOVER || type == DHCPREQUEST) {
        uint8_t stte = dhcpState;
        if((type == DHCPREQUEST && dhcpGetState() == DHCP_REQUESTING) || type == DHCPRELEASE) {
            if(type == DHCPREQUEST && dhcpGetState() == DHCP_REQUESTING) {
                dhcp->options[i++] = 54;
                dhcp->options[i++] = 4;
                for(j = 0; j < 4; j++) {
                    dhcp->options[i++] = serverIdentifierAdd[j];
                }
            }
            dhcp->options[i++] = 50;
            dhcp->options[i++] = 4;
            for(j = 0; j < 4; j++) {
                dhcp->options[i++] = ipOfferedAdd[j];
            }
        }
        if(dhcpGetState() == DHCP_REBINDING || dhcpGetState() == DHCP_RENEWING) {
            uint8_t temp = 8;
        }
        if(dhcpGetState() != DHCP_RENEWING && dhcpGetState() != DHCP_REBINDING) {
            dhcp->options[i++] = 55;
            dhcp->options[i++] = 3;
            dhcp->options[i++] = 1; //subnet mask
            dhcp->options[i++] = 3; //Router option
            dhcp->options[i++] = 6; //dns
        }
    }
    if(type == DHCPDECLINE) {
        dhcp->options[i++] = 50;
        dhcp->options[i++] = 4;
        for(j = 0; j < 4; j++) {
            dhcp->options[i++] = ipOfferedAdd[j];
        }
    }
    //request t1 and t2
    dhcp->options[i++] = DHCPENDOPTION;
    if(i % 2 != 0)
        dhcp->options[i++] = 0;
    /*
     * sn - subnet mask
     * gw - router option (gate way)
     * dns - domain name server
     * time -
     */

    //
    // continue dhcp message here

    // add magic cookie
    // send dhcp message type (53)
    // send requested ip (50) as needed
    // send server ip (54) as needed
    // send parameter request list (55)
        //subnet mask
    // send parameter padding (0)
    // send parameter end option (255) after everything
        //n - size
        //c1 parameter u want back. ex: 1, or 6

    // calculate dhcp size, update ip and udp lengths
    // calculate ip header checksum, calculate udp checksum
    // send packet with size = ether hdr + ip header + udp hdr + dhcp_size
    // send packet
    /*
     * Size calculation
     *  ethernet header size
     *  ip header size -> rev size(lower byte)
     *  udp header size
     *  dhcp header size
     *  dhcp option size
     *
     * ip->length = size of dhcp  + size of udp (which is size of udp header + dhcp size) + size if ipHeader
     *      size of upd = size of udp header + data of upd (dhcp size)
     * Check sum (check etherSendUdpResponse)
     *  calculate IP checksum -> etherCalcIpChecksum;
     *  Calculate UPD check sum
     *      Include
     *          source IP: etherSumWords(ip->source, size, &sum)
     *          destination
     *          Protocol and Zero
     *              temp = ip->protocol
     *              sum += (temp && 0xFF) << 8
     *          UDP Length
     *              etherSumWords(UPD->length, size of it, &sum)
     *          UDP->checkSum = 0;
     *          UDP full check sum
     *              etherSumWords(UDP variable, size of calculated size of UDP, &sum)
     */
    //size calculations
    uint8_t dhcpOptionsSize = i;
    uint16_t dhcpSize = sizeof(dhcpFrame) + dhcpOptionsSize;
    uint16_t udpSize = sizeof(udpHeader) + dhcpSize;
    uint16_t ipSize = ipHeaderLength + udpSize;
    ip->length = htons(ipSize);
    udp->length = htons(udpSize);

    //IP checksum
    etherCalcIpChecksum(ip);
    //udp checksum
    sum = 0;
    etherSumWords(ip->sourceIp, 4, &sum);
    etherSumWords(ip->destIp, 4, &sum);
    uint16_t protocolTemp = ip->protocol;
    sum += (protocolTemp & 0xFF) << 8;
    etherSumWords(&udp->length, 2, &sum);
    udp->check = 0;
    etherSumWords(udp, udpSize, &sum);
    udp->check = getEtherChecksum(sum);


    etherPutPacket(ether, sizeof(etherHeader) + ipHeaderLength + sizeof(udpHeader) + sizeof(dhcpFrame) + (uint8_t)dhcpOptionsSize);
}

uint8_t* getOption(etherHeader *ether, uint8_t option, uint8_t* length)
{
    ipHeader* ip = (ipHeader*)ether->data;
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    dhcpFrame* dhcp = (dhcpFrame*)&udp->data;
    uint8_t * solution;
    uint8_t i = 0;
    uint16_t optionsLength = (uint16_t)(htons(udp->length) - sizeof(udpHeader) - sizeof(dhcpFrame));
    for(i = 0; i < optionsLength; i++) {
        uint8_t current = dhcp->options[i];
        if(dhcp->options[i] == option){
            *length = dhcp->options[i + 1];
            return &dhcp->options[i + 2];
        } else {
            if(dhcp->options[i] != DHCPENDOPTION)
                i = i + dhcp->options[i+1] + 1;
            else if(dhcp->options[i] == DHCPENDOPTION)
                break;
        }
    }
    // suggest this function to programatically extract the field value
    return NULL;
}

// Determines whether packet is DHCP offer response to DHCP discover
// Must be a UDP packet
bool dhcpIsOffer(etherHeader *ether)
{
    ipHeader* ip = (ipHeader*)ether->data;
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    dhcpFrame* dhcp = (dhcpFrame*)&udp->data;
    // return true if destport=68 and sourceport=67, op=2, xid correct, and offer msg
    uint16_t length = udp->length - sizeof(dhcpFrame);
    bool ok = false;
    uint16_t i;
    for(i = 0; i < length; i++) {
        if(dhcp->options[i] == 53) {
            if(dhcp->options[i+2] == DHCPOFFER && htonl(dhcp->xid) == globalXid
                    && htons(udp->sourcePort) == 67 && htons(udp->destPort) == 68 && dhcp->op == 2)
                ok = true;
            break;
        }
        i = i + dhcp->options[i+1] + 1;
    }
    if(ok) {
        for(i = 0; i < 4; i++) {
            ipOfferedAdd[i] = dhcp->yiaddr[i];
        }
        uint8_t optionsLength;
        uint8_t * serverId = getOption(ether, 54, &optionsLength);
        for(i = 0; i < optionsLength; i++) {
            serverIdentifierAdd[i] = serverId[i];
        }
    }
//    if(ok)
//        ok = htonl(dhcp->xid) == globalXid;
    return ok;
}

// Determines whether packet is DHCP ACK response to DHCP request
// Must be a UDP packet
bool dhcpIsAck(etherHeader *ether)
{
    ipHeader* ip = (ipHeader*)ether->data;
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    dhcpFrame* dhcp = (dhcpFrame*)&udp->data;
    // return true if destport=68 and sourceport=67, op=2, xid correct, and ack msg
    uint16_t length = htons(udp->length) - sizeof(dhcpFrame);
    bool ok = false;
    uint16_t i;
    for(i = 0; i < length; i++) {
        uint8_t option = dhcp->options[i];
        uint8_t tyoe = dhcp->options[i+2];
        uint32_t id = htonl(dhcp->xid);
        uint16_t sp = htons(udp->sourcePort);
        uint16_t dt = htons(udp->destPort);
        uint8_t opp = dhcp->op;
        if(dhcp->options[i] == 53 && dhcp->options[i+2] == DHCPACK
                && htonl(dhcp->xid) == globalXid && htons(udp->sourcePort) == 67
                && htons(udp->destPort) == 68 && dhcp->op == 2) {
            ok = true;
            break;
        }
        i = i + dhcp->options[i+1] + 1;
    }
    return ok;
}
bool dhcpIsNak(etherHeader *ether)
{
    ipHeader* ip = (ipHeader*)ether->data;
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    dhcpFrame* dhcp = (dhcpFrame*)&udp->data;
    // return true if destport=68 and sourceport=67, op=2, xid correct, and ack msg
    uint16_t length = htons(udp->length) - sizeof(dhcpFrame);
    bool ok = false;
    uint16_t i;
    for(i = 0; i < length; i++) {
        if(dhcp->options[i] == 53 && dhcp->options[i+2] == DHCPNAK
                && htonl(dhcp->xid) == globalXid && htons(udp->sourcePort) == 67
                && htons(udp->destPort) == 68 && dhcp->op == 2) {
            ok = true;
            break;
        }
        i = i + dhcp->options[i+1] + 1;
    }
    return ok;
}

// Handle a DHCP ACK
void dhcpHandleAck(etherHeader *ether)
{
    ipHeader* ip = (ipHeader*)ether->data;
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    dhcpFrame* dhcp = (dhcpFrame*)&udp->data;
    uint8_t i;
    uint8_t length;
    uint8_t *option = getOption(ether, 1, &length);
    //subnetMask
    if(option != NULL) {
        for(i = 0; i < length; i++) {
            subnetMaskAdd[i] = option[i];
        }
    }
    //Router Option (gate way)
    option = getOption(ether, 3, &length);
    if(option != NULL) {
        for(i = 0; i < length; i++) {
            gateWayAdd[i] = option[i];
        }
    }
    //Domain name server
    option = getOption(ether, 6, &length);
    if(option != NULL) {
        for(i = 0; i < length; i++) {
            domainNameServerAdd[i] = option[i];
        }
    }
    //IpTimeServerAddress
    option = getOption(ether, 42, &length);
    if(option != NULL) {
        for(i = 0; i < length; i++) {
            ipTimeServerAdd[i] = option[i];
        }
    }
    //Lease time
    option = getOption(ether, 51, &length);
    if(option != NULL) {
        uint32_t time = 0;
        //0x2345 5623
        /*
         * 0
         *  0x0000
         *  0x0023
         * 1
         *  0x2300
         *  0x2345
         * 2
         *  0x00234500
         *  0x00234556
         */
        for(i = 0; i < length; i++) {
            time <<= 8;
            time |= option[i];
        }
    //    time = htonl(time);
        leaseTime = time;
        t1LeaseTime = time * 0.5;
        t2LeaseTime = time * 0.875;
    }
    // extract offered IP address
    // store sn, gw, dns, and time from options
    /*
     * sn - subnet mask
     * gw - router option (gate way)
     * dns - domain name server
     */
    // store dns server address for later use
    // store lease, t1, and t2
    // stop new address needed timer, t1 timer, t2 timer
    // start t1, t2, and lease end timers
}

void offerTimerHandler() {
    offerTimerFlag = UNSET;
    dhcpSetState(DHCP_INIT);
    //restarttimer
    restartTimer(offerTimerHandler);
}
void t1TimerHandler() {
    dhcpSetState(DHCP_RENEWING);
    renewTimerFlag = UNSET;
    putsUart0("\nRenewing\n");
}
void t2TimerHandler() {
    dhcpSetState(DHCP_REBINDING);
    rebindTimerFlag = UNSET;
    putsUart0("\nRebinding\n");
}
void t3TimerHandler() {
    dhcpSetState(DHCP_REBINDTOINIT);
}

void renewTimerHandler() {
    renewTimerFlag = UNSET;
}
void rebindTimerHandler() {
    rebindTimerFlag = UNSET;
}
void ipTestingHandler() {
    //change state to bound state
    dhcpSetState(DHCP_BOUND);
    stopTimer(ipTestingHandler);
    putsUart0("\nBound\n");
    addressesFlag = UNSET;
    restartLeaseTimerFlag = SET;
}
void dhcpSendPendingMessages(etherHeader *ether)
{
    // if discover needed, send discover, enter selecting state
    // if request needed, send request
    // if release needed, send release

    uint8_t state = dhcpGetState();
    uint8_t dataZero[4] = {0,0,0,0};
    switch(state) {
        case DHCP_INIT:
            putsUart0("\nStarting\n");
            //reset all flags
//            offerTimerFlag = SET;
//            addressesFlag = UNSET;
            restartLeaseTimerFlag = SET;
            renewTimerFlag = UNSET;
            rebindTimerFlag = UNSET;
            //send discover
            //reset all the flags
            dhcpSendMessage(ether, DHCPDISCOVER);
            dhcpSetState(DHCP_SELECTING);
            //if timer is not already on, set periodic timer of 5 sec and if offer, turn it off
                //if timer is not already on = offerTimeFlag is unset, restartTimer returns false (meaning callback function is not already in fn array)
                    //then start a periodic timer
                /*
                 * function called when timer goes to 0. isr will continue to trigger until entry time is set to 0.
                 * in function, set state back to INIT and set offerTimeFlag flag. that makes sure the timer entry (5 sec) is not reset
                 */
            if(offerTimerFlag) {
                startOneshotTimer(offerTimerHandler, 5);
            }
//            if(!offerTimerFlag && !restartTimer(handler)) {
//                startPeriodicTimer(handler, 5);
//            }
            break;
        case DHCP_REQUESTING:
            break;
        case DHCP_TESTING_IP:
            break;
        case DHCP_BOUND:
            //bind IP address
            //start T1 T2 T3
            //ip, subnet, gate way, dns, time
            /*
             * sn - subnet mask
             * gw - router option (gate way)
             * dns - domain name server
             */
//            uint8_t ipOfferedAdd[4];
//            uint8_t serverIdentifierAdd[4];
//            uint8_t subnetMaskAdd[4];
//            uint8_t gateWayAdd[4];
//            uint8_t domainNameServerAdd[4];
            if(!addressesFlag) {
                etherSetIpAddress(ipOfferedAdd);
                etherSetIpSubnetMask(subnetMaskAdd);
                etherSetIpGatewayAddress(gateWayAdd);
                etherSetIpDnsAddress(domainNameServerAdd);
                etherSetIpTimeServerAddress(ipTimeServerAdd);
                addressesFlag = SET;
            }
            //start T1 T2 T3
            if(restartLeaseTimerFlag) {
//                uint32_t leaseTime;
//                uint32_t t1LeaseTime;
//                uint32_t t2LeaseTime;
//                if(!restartTimer(t1TimerHandler))
                    startOneshotTimer(t1TimerHandler, t1LeaseTime);
                if(!restartTimer(t2TimerHandler))
                    startOneshotTimer(t2TimerHandler, t2LeaseTime);
                if(!restartTimer(t3TimerHandler))
                    startOneshotTimer(t3TimerHandler, leaseTime);
                restartLeaseTimerFlag = UNSET;
            }
            break;
        case DHCP_RENEWING:
            if(!renewTimerFlag) {
                dhcpSendMessage(ether, DHCPREQUEST);
                if(!restartTimer(renewTimerHandler)) {
                    startOneshotTimer(renewTimerHandler, 15);
                }
                renewTimerFlag = SET;
            }
            //start timer to only send it once in a while
            break;
        case DHCP_REBINDING:
            if(!rebindTimerFlag) {
                dhcpSendMessage(ether, DHCPREQUEST);
                if(!restartTimer(rebindTimerHandler))
                    startOneshotTimer(rebindTimerHandler, 15);
            }
            rebindTimerFlag = SET;
            break;
        case DHCP_RELEASING:
            //turn dhcp off if it's on
            //if release
//            if(readEeprom(1) == 0xFFFFFFFF) { //is dhcp on?
                //stop all timers
                stopTimer(t1TimerHandler);
                stopTimer(t2TimerHandler);
                stopTimer(t3TimerHandler);
                stopTimer(renewTimerHandler);
                stopTimer(ipTestingHandler);
                stopTimer(offerTimerHandler);

                //reset internet info

                dhcpSendMessage(ether, DHCPRELEASE);
//                writeEeprom(1, 0); // turn dhcp off
                etherSetIpAddress(dataZero);
                etherSetIpSubnetMask(dataZero);
                etherSetIpGatewayAddress(dataZero);
                etherSetIpDnsAddress(dataZero);
                etherSetIpTimeServerAddress(dataZero);
                dhcpSetState(DHCP_INIT);
//            }
            break;
        case DHCP_REBINDTOINIT:
            stopTimer(t1TimerHandler);
            stopTimer(t2TimerHandler);
            stopTimer(t3TimerHandler);
            stopTimer(renewTimerHandler);
            stopTimer(ipTestingHandler);
            stopTimer(offerTimerHandler);

            //reset internet info
            dhcpSendMessage(ether, DHCPRELEASE);
            writeEeprom(1, 0); // turn dhcp off
            etherSetIpAddress(dataZero);
            etherSetIpSubnetMask(dataZero);
            etherSetIpGatewayAddress(dataZero);
            etherSetIpDnsAddress(dataZero);
            etherSetIpTimeServerAddress(dataZero);
            dhcpSetState(DHCP_INIT);
            break;
        case DHCP_OFF:
            stopTimer(t1TimerHandler);
            stopTimer(t2TimerHandler);
            stopTimer(t3TimerHandler);
            stopTimer(renewTimerHandler);
            stopTimer(ipTestingHandler);
            stopTimer(offerTimerHandler);

            //reset internet info
            dhcpSendMessage(ether, DHCPRELEASE);
            writeEeprom(1, 0); // turn dhcp off
            etherSetIpAddress(dataZero);
            etherSetIpSubnetMask(dataZero);
            etherSetIpGatewayAddress(dataZero);
            etherSetIpDnsAddress(dataZero);
            etherSetIpTimeServerAddress(dataZero);
            dhcpSetState(DHCP_DISABLED);
            break;
    }
}
void dhcpProcessDhcpResponse(etherHeader *ether)
{
    // if offer, send request and enter requesting state

    // if ack, call handle ack, send arp request, enter ip conflict test state

    ipHeader* ip = (ipHeader*)ether->data;
    udpHeader* udp = (udpHeader*)((uint8_t*)ip + ((ip->revSize & 0xF) * 4));
    dhcpFrame* dhcp = (dhcpFrame*)&udp->data;
    if(dhcpIsOffer(ether)) {
        offerTimerFlag = SET;
        stopTimer(offerTimerHandler);
        dhcpSetState(DHCP_REQUESTING);
        dhcpSendMessage(ether, DHCPREQUEST);
        putsUart0("\nRequesting");
    }
    if(dhcpIsAck(ether)) {
        //call handle ack, send arp request and enter conflict test state.
        dhcpHandleAck(ether);
        if(dhcpGetState() == DHCP_RENEWING) {
            dhcpSetState(DHCP_BOUND);
            restartLeaseTimerFlag = SET;
            putsUart0("\nBound\n");
            addressesFlag = UNSET;
        }
        else {
            //send arp
            //change state to IP_TESTING
            //send 3 times to receive a response
            etherSendArpRequest(ether, ipOfferedAdd, ipOfferedAdd);
            //start one shot timer
            if(!restartTimer(ipTestingHandler))
                startOneshotTimer(ipTestingHandler, 5); //should be 15
            dhcpSetState(DHCP_TESTING_IP);
            putsUart0("\nIp testing");
        }
    }
    if(dhcpIsNak(ether)) {
        //go back to init state
        dhcpSetState(DHCP_INIT);
        //stop timers T1, T2, T3 and renewTimer
        stopTimer(t1TimerHandler);
        stopTimer(t2TimerHandler);
        stopTimer(t3TimerHandler);
        stopTimer(renewTimerHandler);
        stopTimer(ipTestingHandler);
        stopTimer(offerTimerHandler);
    }
}

void dhcpProcessArpResponse(etherHeader *ether)
{
   // if in conflict resolution, if a response matches the offered add,
   //  send decline and request new address
    arpPacket *arp = (arpPacket*)ether->data;
//    if(0) {
    if(arp->sourceIp[3] == ipOfferedAdd[3] && dhcpGetState() == DHCP_TESTING_IP) {
        //conflict
            //send decline
            //return to INIT_STATE
        //work on DHCPDECLINE structure
        dhcpSendMessage(ether, DHCPDECLINE);
        putsUart0("\nDeclining\n");
        dhcpSetState(DHCP_INIT);
    }
//    else { //when timer runs out, enter bound state
//        //enter BOUND STATE
//        dhcpSetState(DHCP_BOUND);
//    }
}

// DHCP control functions

void dhcpEnable()
{
    // write code to request new address
    // Set state to DHCP_INIT
    dhcpSetState(DHCP_INIT);
}

void dhcpDisable()
{
    // set state to disabled, stop all timers
    dhcpSetState(DHCP_OFF);
}

bool dhcpIsEnabled()
{
    return (dhcpState != DHCP_DISABLED);
}

void dhcpRequestRenew()
{
    dhcpSetState(DHCP_RENEWING);
    renewTimerFlag = UNSET;
}

void dhcpRequestRebind()
{
    dhcpSetState(DHCP_REBINDING);
    rebindTimerFlag = UNSET;
    putsUart0("\nRebinding\n");
}

void dhcpRequestRelease()
{
    dhcpSetState(DHCP_RELEASING);
}

uint32_t dhcpGetLeaseSeconds()
{
    return leaseTime;
}

