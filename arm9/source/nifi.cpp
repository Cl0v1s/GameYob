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

#define RESET_NIFI { NO_TRANSFER, 0xFF, 0, 0 }

bool nifiEnabled = true;
unsigned char macId;

NIFI nifi = RESET_NIFI;

/**
 * @brief Packet management
 * 
 */

struct BGBPacket {
    unsigned char b1;
    unsigned char b2;
    unsigned char b3;
    unsigned char b4;
    unsigned int i1;
};

void sendPacket(BGBPacket packet) {
    int size = 2+5+sizeof(unsigned int);
    unsigned char buffer[size];
    buffer[0] = 'Y';
    buffer[1] = 'O';
    buffer[2] = macId;
    buffer[3] = packet.b1;
    buffer[4] = packet.b2;
    buffer[5] = packet.b3;
    buffer[6] = packet.b4;
    *((unsigned int*)(buffer+7)) = packet.i1;
    Wifi_RawTxFrame(size, 0x000A, (unsigned short *)buffer);
}

/**
 * @brief Clock management
 * 
 */

unsigned int lastClockPing = 0;
void clockTick() {
    nifi.clock += 1;
    // if uclock overflow
    if(nifi.clock == 0) {
        lastClockPing = 0;
    }
}

bool clockUpdate() {
    if(nifi.clock >= lastClockPing + 500) {
        lastClockPing = nifi.clock;
        return true;
    }
    return false;
}

void clockStart() {
    // start an aproximatively 2Mhz clock
    timerStart(2, ClockDivider_1, CLOCK_TICKS, clockTick);
}

void clockStop() {
    timerStop(2);
}


/**
 * @brief Protocol
 * 
 */

void timeout() {
    nifi.state = TRANSFER_READY;
    nifi.pairBuffer = 0xFF;
    timerStop(3);
}

void setTransferState(unsigned char state) {
    if(state == TRANSER_WAIT && nifi.state != TRANSER_WAIT) {
        clockStop();
        timerStart(3, ClockDivider_64, 5000, timeout); // 3 sec timeout
    }
    if(state != TRANSER_WAIT) {
        timerStop(3);
    }

    nifi.state = state;
}

void setTransferReady() {
    setTransferState(TRANSFER_READY);
    clockStop();
    clockStart();
}

void sendSync1() {
    setTransferState(TRANSER_WAIT);
    BGBPacket sync1 = { SYNC1, nifi.selfBuffer, ioRam[0x02], 0, nifi.clock };
    sendPacket(sync1);
}

void sendSync2() {
    //printLog("Send SYNC2\n");
    BGBPacket sync2 = { SYNC2, nifi.selfBuffer, 0x80, 0, 0 };
    sendPacket(sync2);
}

void sendSync3() {
    //printLog("Send SYNC3\n");
    BGBPacket sync3 = { SYNC3, 0, 42, 42, nifi.clock};
    sendPacket(sync3);
}

void packetHandler(int packetID, int readlength)
{
    static char data[4096];
	Wifi_RxRawReadPacket(packetID, readlength, (unsigned short *)data);
    // we ignore packets coming from us
    if (data[32] != 'Y' || data[33] != 'O' || data[34] == macId) {
        return;
    }

    BGBPacket packet;
    packet.b1 = data[35];
    packet.b2 = data[36];
    packet.b3 = data[37];
    packet.b4 = data[38];
    packet.i1 = *((unsigned int*)(data+39));

    switch (packet.b1)
    {
   case SYNC1:
        {
            sendSync2();
            setTransferState(TRANSER_WAIT);
            nifi.pairBuffer = packet.b2;


            // managing clockDiff
            unsigned int tempDiff;
            if(packet.i1 > nifi.clock) {
                tempDiff = packet.i1 - nifi.clock;
            } else {
                tempDiff = nifi.clock - packet.i1;
            }
            unsigned int waitingTime;
            if(tempDiff > nifi.clockDiff) waitingTime = tempDiff - nifi.clockDiff;
            else waitingTime = nifi.clockDiff - tempDiff;

            timerStart(2, ClockDivider_1, CLOCK_TICKS * (waitingTime + 1), setTransferReady);
            break;
        }
    case SYNC2:
        nifi.pairBuffer = packet.b2;
        timerStart(2, ClockDivider_1, CLOCK_TICKS, setTransferReady);
    break;
   case SYNC3:
        {
            if(packet.i1 > nifi.clock) {
                nifi.clockDiff = packet.i1 - nifi.clock;
            } else {
                nifi.clockDiff = nifi.clock - packet.i1;
            }
            printLog("ClockDiff: %d\n", nifi.clockDiff);
            break;
        }
    default:
        break;
    }
}

int cyclesWithNifi(int cycles) {
    if(!nifiEnabled) return cycles;

    if(nifi.state != NO_TRANSFER && nifi.state != TRANSFER_READY) return 0;
    if(clockUpdate()) sendSync3();

    return cycles;
}

void applyNifi() {
    if(nifi.state != TRANSFER_READY) return;
    printLog("S:0x%02x R:0x%02x\n", nifi.selfBuffer, nifi.pairBuffer);
    ioRam[0x01] = nifi.pairBuffer;
    requestInterrupt(SERIAL);
    ioRam[0x02] &= ~0x80;
    nifi.pairBuffer = 0xFF;
    setTransferState(NO_TRANSFER);
}



/**
 * @brief Legacy enable / disable 
 * 
 */
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

    clockStart();
    nifi = RESET_NIFI;
}

void disableNifi() {
    Wifi_DisableWifi();
    nifiEnabled = false;
    clockStop();
}