#pragma once

extern volatile int linkReceivedData;
extern volatile int linkSendData;
extern volatile bool transferWaiting;
extern volatile bool transferReady;
extern volatile bool receivedPacket;
extern volatile int nifiSendid;
// Don't write directly
extern bool nifiEnabled;

void enableNifi();
void disableNifi();
void sendPacketByte(u8 command, u8 data);

extern int nifiMode;
extern unsigned char nifiBuffer;
void initMaster(unsigned char data);
bool master();
void initSlave(unsigned char data);
bool slave();
