#include <nds.h>
#include <time.h>
#include <climits>
#include "mmu.h"
#include "main.h"
#include "gameboy.h"
#include "gbcpu.h"
#include "console.h"
#include "timer.h"
#include "gbgfx.h"

#include "network.h"

unsigned char macId;
std::queue<BGBPacket> receivedPackets;
std::queue<BGBPacket> sendPackets;
static char incomingData[1032];
bool newData = false;

int lastDsFrameCounter = -1;

void sendPacket(BGBPacket packet) {
    sendPackets.push(packet);
}

bool send() {
    if(lastDsFrameCounter == dsFrameCounter || sendPackets.size() == 0) {
        return false;
    }
    lastDsFrameCounter = dsFrameCounter;
    if((WifiData->stats[WSTAT_DEBUG] & 0x8000) == 0x8000) {
        return false;
    }

    int size = sendPackets.size() * sizeof(BGBPacket) + 4;
    char buffer[size];
    int index = 4;
    buffer[0] = 'Y';
    buffer[1] = 'O';
    buffer[2] = macId;
    buffer[3] = sendPackets.size();
    //printLog("Sending %d packets\n", sendPackets.size());
    while(sendPackets.size() > 0) {
        BGBPacket packet = sendPackets.front();
        memcpy(buffer+index, &packet, sizeof(BGBPacket));
        index += sizeof(BGBPacket);
        sendPackets.pop();
    }

    printLog("S QP: %d QB: %d P: %d A: %d DE: %08x\n", WifiData->stats[WSTAT_TXQUEUEDPACKETS], WifiData->stats[WSTAT_TXQUEUEDBYTES], WifiData->stats[WSTAT_TXPACKETS], WifiData->stats[WSTAT_ARM7_UPDATES], WifiData->stats[WSTAT_DEBUG]);
    /*if((WifiData->stats[WSTAT_DEBUG] & 0x8000) == 0x8000) {
        printLog("Rebooting Network\n");
        disableNetwork();
        swiWaitForVBlank();
        enableNetwork();
        swiWaitForVBlank();
    }*/

    if(Wifi_RawTxFrame(size, 0x0014, (unsigned short*)buffer) != 0);
    return true;
}

void receiveNetwork() {
    if(!newData) return;
    newData = false;
    int size = incomingData[35];
    int index = 36;
    //printLog("Analyzing %d packets\n", size);
    while(size > 0) {
        BGBPacket packet;
        memcpy(&packet, incomingData+index, sizeof(BGBPacket));
        //printLog("P: %d %d %d %d %d\n", packet.b1, packet.b2, packet.b3, packet.b4, packet.i1);
        index += sizeof(BGBPacket);
        receivedPackets.push(packet);
        size--;
    }
}

u16 Wifi_RxReadOffset(s32 base, s32 offset) {
	base+=offset;
	if(base>=(WIFI_RXBUFFER_SIZE/2)) base -= (WIFI_RXBUFFER_SIZE/2);
	return WifiData->rxbufData[base];
}

int lastTime = -1;
bool wasLocked = false;
BGBPacket updateNetwork() {
    if(lastTime != rawTime) {
        printLog("D: %08x\n", WifiData->stats[WSTAT_DEBUG]);
        bool locked = (WifiData->stats[WSTAT_DEBUG] & 0x8000) == 0x8000;
        if(locked && wasLocked) {
            printLog("Rebooting Network\n");
            disableNetwork();
            swiWaitForVBlank();
            enableNetwork();
            swiWaitForVBlank();
            wasLocked = false;
        } else if(locked) {
            wasLocked = true;
        }

        lastTime = rawTime;
    }
    /*
    if((WifiData->stats[WSTAT_DEBUG] & 0x8000) == 0x8000) {

    }
    */

    send();
    receiveNetwork();

    if(receivedPackets.size() == 0) return NULL_PACKET;
    BGBPacket p = receivedPackets.front();
    receivedPackets.pop();
    return p;
}


void packetHandler(int packetID, int readlength)
{
    printLog("R L: %d QP: %d QB: %d\n", WifiData->stats[WSTAT_RXQUEUEDLOST], WifiData->stats[WSTAT_RXQUEUEDPACKETS], WifiData->stats[WSTAT_RXQUEUEDBYTES]);

    char y, o, m;
    unsigned short s1 = Wifi_RxReadOffset(packetID, 16);
    unsigned short s2 = Wifi_RxReadOffset(packetID, 17);

    y = (char)s1;
    o = (char)(s1 >> 8);
    m = (char)s2;

    // we ignore packets coming from us
    if (y != 'Y' || o != 'O' || m == macId) {
        return;
    }

	Wifi_RxRawReadPacket(packetID, readlength, (unsigned short *)incomingData);
    newData = true;
    lastDsFrameCounter = dsFrameCounter;
    //printLog("Got %d packets\n", incomingData[35]);
}

void enableNetwork() {
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
    //printLog("MAC %u (%u)", macId, macLength);

	Wifi_EnableWifi();
	Wifi_RawSetPacketHandler(packetHandler);
	Wifi_SetChannel(10);
}

void disableNetwork() {
    Wifi_DisableWifi();
}