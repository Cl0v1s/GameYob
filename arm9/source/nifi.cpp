#include <nds.h>
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
#include "network.h"

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

#define RESET_NIFI { UNDEFINED, NO_TRANSFER, 0xFF, 0xFF, 0, 0, 0 }

#define STUCK_DELAY USHRT_MAX*20

bool nifiEnabled = true;

NIFI nifi = RESET_NIFI;

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
    if(retried == 0 || rawTime == retried) return; // wait 2 sec
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
        //printLog("Sent transfer request with %02x\n", nifi.selfBuffer);
        BGBPacket sync1 = { SYNC1, nifi.selfBuffer, ioRam[0x02], 0, nifi.cycles + when };
        sendPacket(sync1);
        //swiWaitForVBlank();
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
unsigned int framestuck = 0;
unsigned int lastCycles = 0;
void manageStuck() {
    if(nifi.cycles != lastCycles) {
        framestuck = 0;
    } else {
        framestuck += 1;
        if(framestuck >= STUCK_DELAY) {
            disableNifi();
            framestuck = 0;
        }
    }
    lastCycles = nifi.cycles;
}

void packetHandler(BGBPacket packet) {
    if(packet.b1 == 0) return;
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

void cyclesWithNifi() {
    if(!nifiEnabled) return;
    packetHandler(updateNetwork());
    //manageStuck();
    if(nifi.state == TRANSFER_WAIT) {
        retry();
        setEventCycles(0);
        return;
    } else {
        ping();
    }

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
    //printLog("S:0x%02x R:0x%02x\n", nifi.selfBuffer, nifi.pairBuffer);
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
    enableNetwork();
    nifiEnabled = true;
}

void disableNifi() {
    disableNetwork();
    nifiEnabled = false;
}