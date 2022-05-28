#include <nds.h>
#include <dswifi9.h>
#include <time.h>
#include "nifi.h"
#include "mmu.h"
#include "main.h"
#include "gameboy.h"
#include "gbcpu.h"
#include "console.h"

#define SYNC1 104
#define SYNC2 105
#define SYNC3 106

#define NO_TRANSFER 0
#define TRANSFER_INIT 1
#define TRANSFER_READY 2

/*
    unsigned char buffer[8];
    buffer[0] = 'Y';
    buffer[1] = 'O';
    buffer[2] = macId;
    buffer[3] = ioRam[0x01];
    buffer[4] = sid;
    Wifi_RawTxFrame(8, 0x000A, (unsigned short *)buffer);
*/
bool nifiEnabled = true;
bool nifiReady = false;
unsigned char macId;

unsigned int sendId = 0;
int receivedData = -1;
int transferState = NO_TRANSFER;

struct BGBPacket {
    unsigned char b1;
    unsigned char b2;
    unsigned char b3;
    unsigned char b4;
    unsigned int i1;
    unsigned int i2;
};


void setTransferState(int state) {
    if(state == NO_TRANSFER) {
        receivedData = -1;
        timerStop(2);
    }
    if(transferState == NO_TRANSFER && state != NO_TRANSFER){
        timerStart(2, ClockDivider_64, 10000, timeout);
    }
    //printLog("Going into %d\n", state);
    transferState = state;
}

void timeout() {
    printLog("Transfer Timeout\n");
    setTransferState(NO_TRANSFER);
    receivedData = -1;
}

void sendPacket(BGBPacket packet) {
    int size = 2+sizeof(BGBPacket);
    unsigned char buffer[size];
    buffer[0] = macId;
    buffer[1] = packet.b1 ^ packet.b2 ^ packet.b3 ^ packet.b4;
    buffer[2] = packet.b1;
    buffer[3] = packet.b2;
    buffer[4] = packet.b3;
    buffer[5] = packet.b4;
    *((unsigned int*)(buffer+6)) = packet.i1;
    *((unsigned int*)(buffer+6+sizeof(unsigned int))) = packet.i2;

    Wifi_RawTxFrame(size, 0x000A, (unsigned short *)buffer);
}

void sendSync1() {
    sendId += 1;
    //printLog("Send SYNC1\n");
    setTransferState(TRANSFER_INIT);
    BGBPacket sync1 = { SYNC1, ioRam[0x01], ioRam[0x02], 0, 0, sendId };
    sendPacket(sync1);
}

void sendSync2(unsigned int sid) {
    //printLog("Send SYNC2\n");
    BGBPacket sync2 = { SYNC2, ioRam[0x01], 0x80, 0, 0, sid };
    sendPacket(sync2);
}

void sendSync3(unsigned int sid) {
    //printLog("Send SYNC3\n");
    BGBPacket sync3 = { SYNC3, 42, 42, 42, 0, sid};
    sendPacket(sync3);
}


bool updateNifi() {
    if(!nifiEnabled) return true;
    if(transferState != NO_TRANSFER && transferState != TRANSFER_READY) return false;
    return true;
}

void packetHandler(int packetID, int readlength)
{
    static char data[4096];
	Wifi_RxRawReadPacket(packetID, readlength, (unsigned short *)data);
    // we ignore packets coming from us
    if (data[32] == macId) {
        return;
    }

    BGBPacket packet;
    packet.b1 = data[34];
    packet.b2 = data[35];
    packet.b3 = data[36];
    packet.b4 = data[37];
    packet.i1 = *((unsigned int*)(data+38));
    packet.i2 = *((unsigned int*)(data+38+sizeof(unsigned int)));

    if(packet.b1 != SYNC1 && packet.b1 != SYNC2 && packet.b1 != SYNC3) return;

    unsigned char checksum = packet.b1 ^ packet.b2 ^ packet.b3 ^ packet.b4;
    if(data[33] != checksum) {
        printLog("Bad checksum %u should be %u\n", data[33], checksum);
        return;
    }

    switch (packet.b1)
    {
    case SYNC1:
        setTransferState(TRANSFER_INIT);
        receivedData = packet.b2;
        sendId = packet.i2;
        sendSync2(packet.i2);
        break;
    case SYNC2:
        if(sendId != packet.i2) return;
        setTransferState(TRANSFER_INIT);
        receivedData = packet.b2;
        sendSync3(packet.i2);
        break;
    case SYNC3:
        {
            if(sendId != packet.i2) return;
            if(transferState == TRANSFER_INIT) {
                sendSync3(packet.i2);
                setTransferState(TRANSFER_READY);
            }
            break;
        }
    default:
        break;
    }
}


void applyTransfer() {
    if(receivedData == -1 || transferState != TRANSFER_READY) return;
    printLog("S:0x%02x R:0x%02x\n", ioRam[0x01], receivedData & 0xFF);
    ioRam[0x01] = receivedData & 0xFF;
    requestInterrupt(SERIAL);
    ioRam[0x02] &= ~0x80;
    receivedData = -1;
    setTransferState(NO_TRANSFER);
}


void enableNifi()
{
	Wifi_InitDefault(false);
	Wifi_SetPromiscuousMode(1);
	Wifi_EnableWifi();
    // get identity
    int temp = 0;
    unsigned char macAddress[6];
    int macLength = Wifi_GetData(WIFIGETDATA_MACADDRESS, sizeof(char)*6, macAddress);
    for(int i = 0; i < macLength; i++) {
        temp += macAddress[i];
    }
    time_t rawtime;
    time (&rawtime);
    macId = (temp + (rawtime & 0xFF)) & 0xFF;
    printLog("MAC %u (%u)", macId, macLength);

	Wifi_RawSetPacketHandler(packetHandler);

	Wifi_SetChannel(10);

    nifiEnabled = true;
}

void disableNifi() {
    Wifi_DisableWifi();
    nifiEnabled = false;
    nifiReady = false;
    setTransferState(NO_TRANSFER);
    timerStop(3);
    timerStop(2);
    receivedData = -1;
}