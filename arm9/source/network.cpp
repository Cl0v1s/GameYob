#include <nds.h>
#include <dswifi9.h>
#include "dsgmDSWiFi.h"
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
    if(Wifi_RawTxFrame(size, 0x0014, (unsigned short *)buffer) == -1) {
        printLog("SEND FAILED\n");
        swiWaitForVBlank();
        sendPacket(packet);
    }
}

BGBPacket receivePacket() {
    if(receivedPackets.size() == 0) return NULL_PACKET;
    BGBPacket p = receivedPackets.front();
    receivedPackets.pop();
    //swiWaitForVBlank();
    return p;
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

    receivedPackets.push(packet);
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
    printLog("MAC %u (%u)", macId, macLength);

	Wifi_EnableWifi();
	Wifi_RawSetPacketHandler(packetHandler);
	Wifi_SetChannel(10);
}

void disableNetwork() {
    Wifi_DisableWifi();
}