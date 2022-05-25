#include <nds.h>
#include <dswifi9.h>
#include <time.h>
#include "nifi.h"
#include "mmu.h"
#include "main.h"
#include "gameboy.h"
#include "gbcpu.h"
#include "console.h"

volatile int linkReceivedData=-1;
volatile int linkSendData;
volatile bool transferWaiting = false;
volatile bool transferReady = false;
volatile int nifiSendid = 0;

unsigned char macId;

bool nifiEnabled=true;

volatile bool readyToSend=true;

int lastSendid = 0xff;

unsigned char sendId = 0;
int nifiMode = 0;
unsigned char nifiBuffer = 0x00;

void packetHandler(int packetID, int readlength)
{
    static char data[4096];
	Wifi_RxRawReadPacket(packetID, readlength, (unsigned short *)data);
    if (data[32] != 'Y' || data[33] != 'O') {
        return;
    }
    u8 id = data[34];
    if(id == macId) return;
    u8 val = data[35];
    u8 sid = data[36];
    printLog("rec from %d: %d (%d/%d)\n", id, val, sid, sendId);

    switch(nifiMode){
        // we are slave and we get a packet from master
        case 0x00:
            nifiBuffer = val;
            unsigned char buffer[8];
            buffer[0] = 'Y';
            buffer[1] = 'O';
            buffer[2] = macId;
            buffer[3] = ioRam[0x01];
            buffer[4] = sid;
            Wifi_RawTxFrame(8, 0x000A, (unsigned short *)buffer);
            nifiMode = 0x01;
        break;
        // we are master and we get a packet from slave
        case 0x10:
            if(sid == sendId) {
                nifiBuffer = val;
                nifiMode = 3;
            }
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
    printLog("MAC %d (%d)", macId, macLength);

	Wifi_RawSetPacketHandler(packetHandler);

	Wifi_SetChannel(10);
    nifiEnabled = true;
}

void disableNifi() {
    Wifi_DisableWifi();
    nifiEnabled = false;
}

void initMaster(unsigned char data) {
    printLog("Init master !\n");
    sendId = sendId + 1;
    nifiMode = 0x10;
    nifiBuffer = 0;
    unsigned char buffer[8];
    buffer[0] = 'Y';
    buffer[1] = 'O';
    buffer[2] = macId;
    buffer[3] = data;
    buffer[4] = sendId;
    Wifi_RawTxFrame(8, 0x000A, (unsigned short *)buffer);
}