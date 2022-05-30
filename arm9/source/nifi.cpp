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

#define ALONE 0
#define PAIRED 1
#define WAITING 2
#define SWAP 3

#define HANDSHAKE_REQUEST 101
#define HANDSHAKE_RESPONSE 102
#define DELAY_REQUEST 103
#define DELAY_RESPONSE 104
#define SYNC_REQUEST 105
#define SWAP_REQUEST 106
#define SWAP_ANSWER 107

// timerStart(2, ClockDivider_64, 10000, timeout);

unsigned char macId;
bool nifiEnabled = false;


struct BGBPacket {
    unsigned char b1;
    unsigned char b2;
    unsigned char b3;
    unsigned char b4;
    unsigned int i1;
};

NIFIStruct nifi = { ALONE, 0, 0, 0,0 , 0,  -1};

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

void disableNifi() {
    Wifi_DisableWifi();
    nifiEnabled = false;
}

// =====================================================================
unsigned int delayTemp = 0;
void delayTick() {
    delayTemp += 1;
}


unsigned int handshakeCounter = 2000;
void handshake() {
    handshakeCounter -= 1;
    if(handshakeCounter == 0) {
        delayTemp = 0;
        unsigned int t = (rawTime | 0xFFFFFFFF) + 10;
        BGBPacket request = { HANDSHAKE_REQUEST, 0, 0, 0, t};
        sendPacket(request);
        handshakeCounter = 2000;
    }
}

unsigned int delayCounter = 50000;
void delay() {
    delayCounter -= 1;
    if(delayCounter == 0) {
        delayTemp = 0;
        BGBPacket request = { DELAY_REQUEST, 0, 0, 0, 0};
        sendPacket(request);
        delayCounter = 50000;
    }
}

void stopWait() {
    timerStop(3);
    nifi.state = PAIRED;
}

unsigned int syncCounter = 500;
void sync() {
    syncCounter -= 1;
    if(syncCounter == 0) {
        BGBPacket request = { SYNC_REQUEST, 0, 0, 0, (unsigned int)cyclesTotal};
        sendPacket(request);
        nifi.state = WAITING;
        timerStart(3, ClockDivider_1, nifi.delay/2, stopWait);
        syncCounter = 500;
    }
}

void swap(bool master) {
    unsigned char type = SWAP_REQUEST;
    if(!master) type = SWAP_ANSWER;
    //printLog("Sending %02x\n", nifi.selfBuffer);
    BGBPacket request = { type, nifi.selfBuffer, 0, 0, (unsigned int)cyclesTotal};
    sendPacket(request);
    nifi.state = WAITING;
}

void serial() {
    printLog("S: %02x R: %02x\n", nifi.selfBuffer, nifi.pairBuffer);
    ioRam[0x01] = nifi.pairBuffer;
    ioRam[0x02] &= ~0x80;
    requestInterrupt(SERIAL);
    nifi.cyclesToSerialTransfer = -1;
    nifi.pairBuffer = -1;
    nifi.state = PAIRED;
}

void tryReset() {
    int d = (int)(nifi.resetAt - rawTime);
    printLog("Reset in %ds\n", d);
    if(d == 0) {
        nifi.resetAt = 0;
        resetGameboy();
    }
}

int cyclesToWait = 0;

int updateNifi(int cycles) {
    if(!nifiEnabled) return cycles;

    if(nifi.state == SWAP) {
        if(ioRam[0x02] & 0x01) {
            if(nifi.cyclesToSerialTransfer != -1) {
                cyclesToWait = cyclesTotal - nifi.cyclesToSerialTransfer;
                nifi.cyclesToSerialTransfer = -1;
            }
            if(cyclesToWait <= 0) {
                serial();
            }
        } else {
            if(nifi.cyclesToSerialTransfer > cyclesTotal) {
                return nifi.cyclesToSerialTransfer - cyclesTotal;
            } else {
                serial();
            }
        }
    } else if(nifi.state == WAITING) {
        return 0;
    } else if(nifi.state == ALONE) {
        handshake();
    } else {
        if(nifi.resetAt != 0) {
            tryReset();
            return cycles;
        }
        delay();
        sync();

        if(nifi.pairTotalCycles != 0 && cyclesTotal > nifi.pairTotalCycles) {
            cyclesToWait = cyclesTotal - nifi.pairTotalCycles;
            nifi.pairTotalCycles = 0;
        }
    }

    if(cyclesToWait > 0) {
        if(cyclesToWait >= cycles) {
            cyclesToWait = cyclesToWait - cycles;
            return 0;
        } else {
            cyclesToWait = 0;
            return cycles - cyclesToWait;
        }
    }



    return cycles;
}

void handleHandshake(BGBPacket packet) {
    nifi.resetAt = rawTime | packet.i1;
    nifi.state = PAIRED;
}

void handleSwap(BGBPacket packet) {
    nifi.pairTotalCycles = packet.i1;
    /*
    if(ioRam[0x02] & 0x01) {
        //printLog("Swap as master\n");
        // if we are master and slave is in advance, we ignore the swap
        if(nifi.pairTotalCycles > cyclesTotal) {
            printLog("Canceled %d\n", (int)(nifi.pairTotalCycles - cyclesTotal));
            ioRam[0x01] = 0xFF;
            ioRam[0x02] &= ~0x80;
            requestInterrupt(SERIAL);
            nifi.state = PAIRED;
            return;
        }
    } else {
        //printLog("Swap as slave\n");
        // if we are slave and master is too late, we ignore the swap
        if(nifi.pairTotalCycles < cyclesTotal) {
            printLog("Canceled %d\n", (int)(cyclesTotal - nifi.pairTotalCycles));
            nifi.state = PAIRED;
            return;
        };
    }*/

    nifi.cyclesToSerialTransfer = packet.i1;
    nifi.pairBuffer = packet.b2;
    nifi.state = SWAP;
}

void handleSwapRequest(BGBPacket packet) {
    swap(false);
    handleSwap(packet);
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
        case HANDSHAKE_REQUEST:
            {
                BGBPacket response = { HANDSHAKE_RESPONSE, 0, 0, 0, packet.i1};
                sendPacket(response);
            }
        case HANDSHAKE_RESPONSE:
            nifi.delay = delayTemp;
            handleHandshake(packet);
            break;
        case DELAY_REQUEST:
            {
                BGBPacket response = { DELAY_RESPONSE, 0, 0, 0, 0};
                sendPacket(response); 
                break;
            }
        case DELAY_RESPONSE:
            nifi.delay = delayTemp;
            //printLog("Delay %d ticks\n", nifi.delay);
            break;
        case SYNC_REQUEST:
            nifi.pairTotalCycles = (int)packet.i1;
            break;
        case SWAP_REQUEST:
            handleSwapRequest(packet);
            break;
        case SWAP_ANSWER:
            handleSwap(packet);
            break;
    default:
        break;
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

    timerStart(2, ClockDivider_1, 1, delayTick);

}