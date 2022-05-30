#include <nds.h>
#include <dswifi9.h>
#include <time.h>
#include "nifi.h"
#include "mmu.h"
#include "main.h"
#include "gameboy.h"
#include "gbcpu.h"
#include "console.h"
#include "timer.h"

// timerStart(2, ClockDivider_64, 10000, timeout);

#define SYNC0 103
#define SYNC1 104
#define SYNC2 105
#define SYNC3 106

#define PAIR_NO 0
#define PAIR_PAIRING 1
#define PAIR_PAIRED 2
#define PAIR_COUNTER 500

int nifiEnable = false;
int nifiPairState = PAIR_NO;
time_t nifiPairRestartAt = 0;

unsigned int nifiUpdateCounter = PAIR_COUNTER;


struct BGBPacket {
    unsigned char b1;
    unsigned char b2;
    unsigned char b3;
    unsigned char b4;
    unsigned int i1;
};

void sendPacket(BGBPacket packet) {
    int size = 5+sizeof(unsigned int);
    unsigned char buffer[size];
    buffer[0] = macId;
    buffer[1] = packet.b1;
    buffer[2] = packet.b2;
    buffer[3] = packet.b3;
    buffer[4] = packet.b4;
    *((unsigned int*)(buffer+5)) = packet.i1;
    Wifi_RawTxFrame(size, 0x000A, (unsigned short *)buffer);
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
    packet.b1 = data[33];
    packet.b2 = data[34];
    packet.b3 = data[35];
    packet.b4 = data[36];
    packet.i1 = *((unsigned int*)(data+37));

    switch (packet.b1)
    {
    default:
        break;
    }
}

void cancelPairing() {
    nifiPairState = PAIR_NO;
}

void sendSync0() {
    // other side restartAt = rawTime | receivedRestartAt
    unsigned int restartAt = (rawTime << (sizeof(time_t) - sizeof(unsigned int))) + 10;
    BGBPacket sync0 = {SYNC0, 0, 0, 0, restartAt};
    sendPacket(sync0);
    timerStart(2, ClockDivider_1024, 5, cancelPairing);

}

void updateNifi() {
    if(nifiPairState == PAIR_NO) {
        nifiUpdateCounter -= 1;
        if(nifiUpdateCounter == 0) {
            sendSync0();
            nifiUpdateCounter = PAIR_COUNTER;
        }
    }
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
}