#ifndef TCPCLIENT_H_
#define TCPCLIENT_H_

void tcpClientSendMessage(etherHeader *ether, uint8_t type);
void getDnsData(etherHeader *ether, uint8_t ipFrom[4]);

#endif
