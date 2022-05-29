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
#define TRANSER_WAIT 1
#define TRANSFER_READY 2

#define CLOCK_TICKS 15
#define WAITING_MIN 55535/CLOCK_TICKS

bool nifiEnabled = true;
bool nifiReady = false;
unsigned char macId;

int receivedData = -1;
int transferState = NO_TRANSFER;

unsigned int lastClockPing = 0;
unsigned int uclock = 0;
unsigned int clockDiff = 0;

struct BGBPacket {
    unsigned char b1;
    unsigned char b2;
    unsigned char b3;
    unsigned char b4;
    unsigned int i1;
};

void clockTick() {
    uclock += 1;
    // if uclock overflow
    if(uclock == 0) {
        lastClockPing = 0;
    }
}

void setTransferState(int state) {
    if(state != transferState) {
        cyclesToExecute = -1;
    }

    if(state == TRANSER_WAIT) {
        // we stop clock when we are waiting
        timerStop(3);
        timerStart(2, ClockDivider_64, 10000, timeout);
    } else {
        timerStop(2);
        timerStart(3, ClockDivider_1, CLOCK_TICKS, clockTick);
    }

    /*if(state == NO_TRANSFER) {
        receivedData = -1;
        timerStop(2);
    }
    if(transferState == NO_TRANSFER && state != NO_TRANSFER){
        timerStart(2, ClockDivider_64, 10000, timeout);
    }
    */
    // printLog("Going into %d\n", state);
    transferState = state;
}



void timeout() {
    printLog("Transfer Timeout\n");
    receivedData = 0xFF;
    setTransferState(TRANSFER_READY);
}

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

void sendSync1() {
    setTransferState(TRANSER_WAIT);
    BGBPacket sync1 = { SYNC1, ioRam[0x01], ioRam[0x02], 0, uclock };
    sendPacket(sync1);
}

void sendSync2() {
    //printLog("Send SYNC2\n");
    BGBPacket sync2 = { SYNC2, ioRam[0x01], 0x80, 0, 0 };
    sendPacket(sync2);
}

void sendSync3() {
    //printLog("Send SYNC3\n");
    BGBPacket sync3 = { SYNC3, 42, 42, 42, uclock};
    sendPacket(sync3);
}

void setTransferReady() {
    setTransferState(TRANSFER_READY);
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
   case SYNC1:
        {
            setTransferState(TRANSER_WAIT);
            receivedData = packet.b2;

            sendSync2();

            // managing clockDiff
            unsigned int tempDiff;
            if(packet.i1 > uclock) {
                tempDiff = packet.i1 - uclock;
            } else {
                tempDiff = uclock - packet.i1;
            }
            unsigned int waitingTime;
            if(tempDiff > clockDiff) waitingTime = tempDiff - clockDiff;
            else waitingTime = clockDiff - tempDiff;

            clockDiff = tempDiff;

            timerStart(3, ClockDivider_1, CLOCK_TICKS * (waitingTime + WAITING_MIN), setTransferReady);
            break;
        }
    case SYNC2:
        receivedData = packet.b2;
        timerStart(3, ClockDivider_1, CLOCK_TICKS * (WAITING_MIN), setTransferReady);
    break;
   case SYNC3:
        {
            if(packet.i1 > uclock) {
                clockDiff = packet.i1 - uclock;
            } else {
                clockDiff = uclock - packet.i1;
            }
            //printLog("ClockDiff set at %u\n", clockDiff);
            break;
        }
    default:
        break;
    }
}

bool updateNifi() {
    if(!nifiEnabled) return true;

    if(transferState != NO_TRANSFER && transferState != TRANSFER_READY) return false;

    // update clockDiff between other and us
    if(uclock > lastClockPing + 100) {
        sendSync3();
        lastClockPing = uclock;
    }

    return true;
}

bool applyTransfer() {
    if(receivedData == -1 || transferState != TRANSFER_READY) return false;
    printLog("S:0x%02x R:0x%02x\n", ioRam[0x01], receivedData & 0xFF);
    ioRam[0x01] = receivedData & 0xFF;
    requestInterrupt(SERIAL);
    ioRam[0x02] &= ~0x80;
    receivedData = -1;
    setTransferState(NO_TRANSFER);
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
    timerStart(3, ClockDivider_1, 10, clockTick);
}

void disableNifi() {
    Wifi_DisableWifi();
    nifiEnabled = false;
    nifiReady = false;
    setTransferState(NO_TRANSFER);
    timerStop(3);
    // timerStop(2);
    // receivedData = -1;
}