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

#define RESET_DELAY 5
#define GAP 50

#define MASTER 1
#define SLAVE 2

#define ALONE 0
#define PAIRING 1
#define PAIRED 2
#define WAITING 3
#define SWAP 4

#define HANDSHAKE_REQUEST 101
#define HANDSHAKE_RESPONSE 102
#define DELAY_REQUEST 103
#define DELAY_RESPONSE 104
#define SYNC_REQUEST 105
#define SWAP_REQUEST 106
#define SWAP_RESPONSE 107

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

NIFIStruct nifi = { 0, ALONE, 0, 0, 0,0 , 0,  -1};

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
int delayTemp = 0;

unsigned int handshakeCounter = 2000;
void handshake() {
    handshakeCounter -= 1;
    if(handshakeCounter == 0) {
        handshakeCounter = 2000;
        unsigned int t = ((rawTime + RESET_DELAY) & 0xFFFFFFFF);
        if((int)(t - rawTime) != RESET_DELAY) return;

        delayTemp = 0;
        BGBPacket request = { HANDSHAKE_REQUEST, 0, 0, 0, t};
        sendPacket(request);
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

unsigned int syncCounter = 500;
void sync() {
    syncCounter -= 1;
    if(syncCounter == 0) {
        BGBPacket request = { SYNC_REQUEST, 0, 0, 0, (unsigned int)cyclesTotal};
        sendPacket(request);
        syncCounter = 500;
    }
}

void swap(bool master) {
    unsigned char type = SWAP_REQUEST;
    if(!master) type = SWAP_RESPONSE;
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
    //printLog("Reset in %ds\n", d);
    if(d == 0) {
        printLog("Reseting as %u\n", nifi.master);
        nifi.resetAt = 0;
        resetGameboy();
    }
}

int cyclesToWait = 0;
void manageWait() {
    if(nifi.pairTotalCycles == 0 ) return;
    //printLog("Diff %d\n", abs(cyclesTotal - nifi.pairTotalCycles));
    if(nifi.master == MASTER) {
        if(cyclesTotal >= nifi.pairTotalCycles + nifi.delay + GAP) {
            cyclesToWait = cyclesTotal - nifi.pairTotalCycles + GAP;
            printLog("Master waiting %d cycles\n", cyclesToWait);
        }
    } else {
        if(cyclesTotal >= nifi.pairTotalCycles - nifi.delay - GAP) {
            cyclesToWait = cyclesTotal - nifi.pairTotalCycles + GAP;
            printLog("Slave waiting %d cycles\n", cyclesToWait);
        }
    }
    nifi.pairTotalCycles = 0;
}


int updateNifi(int cycles) {
    if(!nifiEnabled) return cycles;



    if(nifi.state == WAITING) {
        cycles = 0;
    } else if(nifi.state == ALONE) {
        handshake();
    } else if(nifi.state == SWAP) {
        printLog("%d cycles before serial\n", nifi.cyclesToSerialTransfer);
        if(nifi.cyclesToSerialTransfer <= 0) {
            BGBPacket request = { SWAP_RESPONSE, nifi.selfBuffer, 0, 0, (unsigned int)cyclesTotal};
            sendPacket(request);
            serial();
            return 0;
        }

        if(cycles > nifi.cyclesToSerialTransfer)  {
            cycles = cycles - nifi.cyclesToSerialTransfer;
            nifi.cyclesToSerialTransfer = 0;
        } else {
            nifi.cyclesToSerialTransfer = nifi.cyclesToSerialTransfer - cycles;
        }
    } else if (nifi.state == PAIRING) {
        if(nifi.resetAt != 0) {
            tryReset();
            return cycles;
        }
        if(cyclesTotal == 0) {
            nifi.state = PAIRED;
            return 0;
        }
    } else {
        if(nifi.master == SLAVE) cycles = 1;
        delay();
        sync();
        manageWait();
    }

    if(cyclesToWait > 0) {
        if(cyclesToWait >= cycles) {
            cyclesToWait = cyclesToWait - cycles;
            cycles = 0;
        } else {
            cyclesToWait = 0;
            cycles = cycles - cyclesToWait;
        }
    }

    delayTemp += cycles;

    return cycles;
}

void handleHandshake(BGBPacket packet, unsigned char master) {
    time_t resetAt = rawTime | packet.i1;
    int d = (int)(resetAt - rawTime);
    if(d < 0) return;

    printLog("Reset in %ds\n", d);
    nifi.state = PAIRING;
    nifi.master = master;
    if(master == MASTER) { 
        printLog("Master\n"); 
    }
    else { 
        printLog("Slave\n"); 
    }
    nifi.resetAt = resetAt;
}

void handleSwapResponse(BGBPacket packet) {
    nifi.pairTotalCycles = ((int)packet.i1);
    if(nifi.pairTotalCycles > cyclesTotal) {
        printLog("Canceled %d\n", (int)(nifi.pairTotalCycles - cyclesTotal));
        nifi.pairBuffer = 0xFF;
        serial();
        return;
    };

    nifi.pairBuffer = packet.b2;
    serial();
}

void handleSwapRequest(BGBPacket packet) {
    nifi.pairTotalCycles = ((int)packet.i1);
    // if we are slave and master is too late, we ignore the swap
    if(nifi.pairTotalCycles < cyclesTotal) {
        printLog("Canceled %d\n", (int)(cyclesTotal - nifi.pairTotalCycles));
        nifi.state = PAIRED;
        return;
    };

    nifi.cyclesToSerialTransfer = nifi.pairTotalCycles - cyclesTotal;

    nifi.pairBuffer = packet.b2;
    nifi.state = SWAP;
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
                if(nifi.master != 0) return;
                BGBPacket response = { HANDSHAKE_RESPONSE, 0, 0, 0, packet.i1};
                sendPacket(response);
                handleHandshake(packet, SLAVE);
            }
        case HANDSHAKE_RESPONSE:
            if(nifi.master != 0) return;
            nifi.delay = delayTemp;
            handleHandshake(packet, MASTER);
            break;
        case DELAY_REQUEST:
            {
                BGBPacket response = { DELAY_RESPONSE, 0, 0, 0, 0};
                sendPacket(response); 
                break;
            }
        case DELAY_RESPONSE:
            nifi.delay = (nifi.delay + delayTemp) / 2;
            //printLog("Delay %d cycles mean\n", nifi.delay);
            break;
        case SYNC_REQUEST:
            nifi.pairTotalCycles = ((int)packet.i1);
            break;
        case SWAP_REQUEST:
            handleSwapRequest(packet);
            break;
        case SWAP_RESPONSE:
            handleSwapResponse(packet);
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
}