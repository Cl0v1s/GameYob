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
static char incomingData[1032];
bool newData = false;

int lastDsFrameCounter = -1;

void sendPacket(BGBPacket packet) {
    sendPacket.push(packet);
}

bool send() {
    if(lastDsFrameCounter == dsFrameCounter) {
        return false;
    }
    lastDsFrameCounter = dsFrameCounter;

    int size = sendPackets.size() * sizeof(BGBPacket) + 2;
    char buffer[size];
    int index = 4;
    buffer[0] = 'Y';
    buffer[1] = 'O';
    buffer[2] = macId;
    buffer[3] = sendPackets.size();
    printLog("Sending %d packets\n", sendPackets.size());
    while(sendPackets.size() > 0) {
        BGBPacket packet = sendPackets.front();
        memcpy(buffer+index, &packet, sizeof(BGBPacket));
        index += sizeof(BGBPacket);
        sendPackets.pop();
    }

    Wifi_RawTxFrame(size, 0x0014, (unsigned short*)buffer);
}

void receive() {
    if(!newData) return;
    newData = false;
    int size = incomingData[35];
    int index = 36;
    printLog("Analyzing %d packets\n", size);
    while(size > 0) {
        BGBPacket packet;
        memcpy(&packet, incomingData+index, sizeof(BGBPacket));
        printLog("P: %d %d %d %d %d\n", packet.b1, packet.b2, packet.b3, packet.b4, packet.i1);
        index += sizeof(BGBPacket);
        receivedPackets.push(packet);
        size--;
    }
}

BGBPacket updateNetwork() {
    send();
    receive();

    if(receivedPackets.size() == 0) return NULL_PACKET;
    BGBPacket p = receivedPackets.front();
    receivedPackets.pop();
    return p;
}


void packetHandler(int packetID, int readlength)
{
    static char data[1032];
	Wifi_RxRawReadPacket(packetID, readlength, (unsigned short *)data);
    // we ignore packets coming from us
    if (data[32] != 'Y' || data[33] != 'O' || data[34] == macId) {
        return;
    }
    newData = true;
    lastDsFrameCounter = dsFrameCounter;
    memcpy(incomingData, data, 1032);
    printLog("Got %d packets\n", data[35]);

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