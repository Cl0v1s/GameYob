#include <nds.h>
#include <dswifi9.h>
#include "dsgmDSWiFi.h"
#include <time.h>
#include <climits>
#include "nifi.h"
#include "mmu.h"
#include "main.h"
#include "gameboy.h"
#include "gbcpu.h"
#include "console.h"
#include "timer.h"
#include "gbgfx.h"



#define UNDEFINED 0
#define LEADER 1
#define FOLLOWER 2

#define SYNC1 104
#define SYNC2 105
#define SYNC3 106
#define SYNC4 107
#define SYNC5 108

#define NO_TRANSFER 0
#define TRANSFER_WAIT 1
#define TRANSFER_READY 2

#define CLOCK_TICKS 15

#define RESET_NIFI { UNDEFINED, NO_TRANSFER, 0xFF, 0xFF, 0, 0, 0 }

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
    Wifi_RawTxFrame(size, 0x0014, (unsigned short *)buffer);
}
/**
 * @brief Protocol
 * 
 */

time_t retried = 0;

void enableRetry() {
    retried = rawTime;
}

void disableRetry() {
    retried = 0;
}

void retry() {
    if(retried == 0 || rawTime - retried < 2) return; // wait 2 sec
    printLog("retry\n");
    sendSync1(0);
    retried = rawTime;
}

void setTransferState(unsigned char state) {
    if(nifi.state != TRANSFER_WAIT && state == TRANSFER_WAIT) {
        enableRetry();
    } else if(state != TRANSFER_WAIT) {
        disableRetry();
    }
    nifi.state = state;
}

void sendSync1(int when) {
    if(nifi.type == LEADER) {
        setTransferState(TRANSFER_WAIT);
        printLog("Sent transfer request with %02x\n", nifi.selfBuffer);
        BGBPacket sync1 = { SYNC1, nifi.selfBuffer, ioRam[0x02], 0, nifi.cycles + when };
        sendPacket(sync1);
    }
}

void sendSync2() {
    //printLog("Response transfer request with %02x\n", nifi.selfBuffer);
    BGBPacket sync2 = { SYNC2, nifi.selfBuffer, 0x80, 0, 0 };
    sendPacket(sync2);
}

void sendSync3() {
    BGBPacket sync3 = { SYNC3, 0, 42, 42, nifi.cycles };
    sendPacket(sync3);
}

void sendSync4() {
    BGBPacket sync4 = { SYNC4, 0, 42, 42, 0};
    sendPacket(sync4);
}

void sendSync5(unsigned int cycles) {
    BGBPacket sync5 = { SYNC5, 0, 42, 42, cycles};
    sendPacket(sync5);
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
            nifi.cylesToSerialTransfer = packet.i1;
            // printLog("Received request for %u\n", nifi.cylesToSerialTransfer - nifi.cycles);
            nifi.pairBuffer = packet.b2;
            break;
        }
    case SYNC2:
        nifi.pairBuffer = packet.b2;
        disableRetry();
        setTransferState(TRANSFER_READY);
    break;
   case SYNC3:
        {
            nifi.pairCycles = packet.i1;
            break;
        }
    case SYNC4: 
        {
            nifi.type = FOLLOWER;
            nifi.cycles = 0;
            nifi.pairCycles = 0;
            nifi.cylesToSerialTransfer = 0;
            disableRetry();
            sendSync5(packet.i1);
            printLog("Reset as FOLLOWER\n");
            resetGameboy();
            break;
        }
    case SYNC5:
        {
            if(nifi.type == UNDEFINED) {
                nifi.type = LEADER;
                disableRetry();
                printLog("Reset as LEADER\n");
                resetGameboy();
            }
            break;
        }
    default:
        break;
    }
}

/**
 * @brief Ping management
 * 
 */
time_t lastPing = 0;
void ping() {
    if(lastPing == rawTime) return;
    lastPing = rawTime;
    //printLog("%u cycles\n", nifi.cycles);
    if(nifi.type == UNDEFINED) {
        sendSync4();
    } else if(nifi.type == LEADER) {
        sendSync3();
    }
}

/**
 * @brief Cycles management
 */
void cyclesWithNifi() {
    if(!nifiEnabled) return;

    if(nifi.state == TRANSFER_WAIT) {
        retry();
        setEventCycles(0);
        return;
    } else {
        ping();
    }

    // code below is only for follower
    if(nifi.type == LEADER) return;

    if(nifi.cylesToSerialTransfer > 0) {
        if(nifi.cylesToSerialTransfer <= nifi.cycles) {
            sendSync2();
            applyNifi();
            setEventCycles(0);
        } else {
            // manage max speed so we stay behind the leader
            unsigned int diff = nifi.cylesToSerialTransfer - nifi.cycles;
            if(diff > INT_MAX) {
                diff = INT_MAX;
            }
            setEventCycles((int)diff);
        }
    } else if(nifi.pairCycles > 0) {
        // we are too fast
        if(nifi.pairCycles < nifi.cycles) {
            // prevent follower from being stuck for too long
            setEventCycles(0);
        } else {
            // manage max speed so we stay behind the leader
            unsigned int diff = nifi.pairCycles - nifi.cycles;
            if(diff > INT_MAX) {
                diff = INT_MAX;
            }
            setEventCycles((int)diff);
        }
    }


}

void waitForNifi() {
    if(nifi.type == LEADER) {
        if(nifi.state != TRANSFER_READY) {
            setTransferState(TRANSFER_WAIT);
        } else applyNifi();
    } else {
        // if we are here, FOLLOWER tried to do a transfer as master
        // if no transfer is asked by LEADER, always answer with 0xFF
        if(nifi.cylesToSerialTransfer == 0) {
            nifi.pairBuffer = 0xFF;
            applyNifi();
        }
    }
}

void applyNifi() {
    printLog("S:0x%02x R:0x%02x\n", nifi.selfBuffer, nifi.pairBuffer);
    ioRam[0x01] = nifi.pairBuffer;
    requestInterrupt(SERIAL);
    ioRam[0x02] &= ~0x80;
    // nifi.pairBuffer = 0xFF;
    // nifi.selfBuffer = 0xFF;
    nifi.cylesToSerialTransfer = 0;
    setTransferState(NO_TRANSFER);
    if(nifi.type == LEADER) sendSync3();
}
/**
 * @brief Legacy enable / disable 
 * 
 */
int originalInterruptWaitMode = 0;
void enableNifi()
{
    wirelessMode = WIRELESS_MODE_NIFI;
	Wifi_InitDefault(false);
	Wifi_SetPromiscuousMode(1);
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

	Wifi_EnableWifi();
	Wifi_RawSetPacketHandler(packetHandler);
	Wifi_SetChannel(10);
    nifiEnabled = true;
    nifi = RESET_NIFI;

    // always wait for Vblank
    originalInterruptWaitMode = interruptWaitMode;
    interruptWaitMode = 1;
}

void disableNifi() {
    printLog("Disabling Nifi\n");
    Wifi_DisableWifi();
    nifiEnabled = false;

    interruptWaitMode = originalInterruptWaitMode;
}