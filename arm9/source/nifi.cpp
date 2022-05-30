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
#define ACK 107

#define PAIR_NO 0
#define PAIR_PAIRING 1
#define PAIR_PAIRED 2
#define PAIR_SYNCING 3
#define PAIR_COUNTER 500

int nifiEnable = false;
int nifiPairState = PAIR_NO;
time_t nifiPairRestartAt = 0;

unsigned int nifiUpdateCounter = PAIR_COUNTER;
unsigned int nifiDelay = 0;

void delayTick() {
    nifiDelay += 1;
}


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
        case ACK:
            {
                if(nifiPairState == PAIR_PAIRING) {
                    //nifiDelay now stores the delay in real time between the two GB
                    stopTimer(2);
                    stopTimer(3);
                    unsigned int restartAt = packet.i1;
                    nifiPairRestartAt = rawTime | restartAt;
                    nifiPairState = PAIR_PAIRED;
                    nifiUpdateCounter = PAIR_COUNTER;
                    printLog("Reset scheduled\n");
                } else if(nifiPairState == PAIR_SYNCING) {
                    //nifiDelay now stores the delay in real time between the two GB
                    stopTimer(2);
                    stopTimer(3);
                    nifiPairState = PAIR_PAIRED;
                    nifiUpdateCounter = PAIR_COUNTER;
                }
                break;
            }
    default:
        break;
    }
}

void cancelPairing() {
    nifiPairState = PAIR_NO;
    nifiUpdateCounter = PAIR_COUNTER;
}

void sendSync0() {
    nifiDelay = 0;
    unsigned int restartAt = (rawTime & 0xFFFFFFFF) + 10;
    BGBPacket sync0 = {SYNC0, 0, 0, 0, restartAt};
    sendPacket(sync0);
    nifiPairState = PAIR_PAIRING;
    timerStart(2, ClockDivider_1024, 5, cancelPairing);
    timerStart(3, ClockDivider_1, 1, delayTick);
}

void cancelSyncing() {
    nifiPairState = PAIR_PAIRED;
    nifiUpdateCounter = PAIR_COUNTER;
}

void sendSync1() {
    nifiDelay = 0;
    BGBPacket sync1 = {SYNC1, 0, 0, 0, cyclesTotal};
    sendPacket(sync1);
    nifiPairState = PAIR_SYNCING;
    timerStart(2, ClockDivider_1024, 5, cancelSyncing);
    timerStart(3, ClockDivider_1, 1, delayTick);
}

void updateNifi() {
    // regular ping to check for pair
    if(nifiPairState == PAIR_NO) {
        nifiUpdateCounter -= 1;
        if(nifiUpdateCounter == 0) {
            sendSync0();
        }
    } else if(nifiPairState == PAIR_PAIRED) { // if paired and reset scheduled, we check if it is time
        if(rawTime == nifiPairRestartAt) { // if it is, we reset
            resetGameboy();
            nifiPairRestartAt = 0;
            printLog("Resetting\n");
        }
        // regular send cycleTotal to pair to maintain sync
        nifiUpdateCounter -= 1;
        if(nifiUpdateCounter == 0) {
            sendSync1();
        }
    }

    return true;
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