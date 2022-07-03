#pragma once
#define SGIP_DEBUG

#include <dswifi9.h>
#include "dsgmDSWiFi.h"
#include <queue>

#define NULL_PACKET { 0, 0, 0, 0 ,0}


#define WIFI_MAX_AP			32
#define WIFI_MAX_PROBE		4
#define WIFI_RXBUFFER_SIZE	(1024*12)
#define WIFI_TXBUFFER_SIZE	(1024*24)


typedef struct WIFI_MAINSTRUCT {
	unsigned long dummy1[8];
	// wifi status
	u16 curChannel, reqChannel;
	u16 curMode, reqMode;
	u16 authlevel,authctr;
	vu32 flags9, flags7;
	u32 reqPacketFlags;
	u16 curReqFlags, reqReqFlags;
	u32 counter7,bootcounter7;
	u16 MacAddr[3];
	u16 authtype;
	u16 iptype,ipflags;
	u32 ip,snmask,gateway;

	// current AP data
	char ssid7[34],ssid9[34];
	u16 bssid7[3], bssid9[3];
	u8 apmac7[6], apmac9[6];
	char wepmode7, wepmode9;
	char wepkeyid7, wepkeyid9;
	u8 wepkey7[20],wepkey9[20];
	u8 baserates7[16], baserates9[16];
	u8 apchannel7, apchannel9;
	u8 maxrate7;
	u16 ap_rssi;
	u16 pspoll_period;

	// AP data
	Wifi_AccessPoint aplist[WIFI_MAX_AP];

	// probe stuff
	u8 probe9_numprobe;
	u8 probe9_ssidlen[WIFI_MAX_PROBE];
	char probe9_ssid[WIFI_MAX_PROBE][32];

	// WFC data
	u8 wfc_enable[4]; // wep mode, or 0x80 for "enabled"
	Wifi_AccessPoint wfc_ap[3];
	unsigned long wfc_config[3][5]; // ip, gateway, snmask, primarydns, 2nddns
	u8 wfc_wepkey[3][16];
	

	// wifi data
	u32 rxbufIn, rxbufOut; // bufIn/bufOut have 2-byte granularity.
	u16 rxbufData[WIFI_RXBUFFER_SIZE/2]; // send raw 802.11 data through! rxbuffer is for rx'd data, arm7->arm9 transfer

	u32 txbufIn, txbufOut;
	u16 txbufData[WIFI_TXBUFFER_SIZE/2]; // tx buffer is for data to tx, arm9->arm7 transfer

	// stats data
	u32 stats[NUM_WIFI_STATS];
   
	u16 debug[30];

   u32 random; // semirandom number updated at the convenience of the arm7. use for initial seeds & such.

	unsigned long dummy2[8];

} Wifi_MainStruct;

extern volatile Wifi_MainStruct * WifiData;

struct BGBPacket {
    unsigned char b1;
    unsigned char b2;
    unsigned char b3;
    unsigned char b4;
    unsigned int i1;
};

extern unsigned char macId;
extern std::queue<BGBPacket> receivedPackets;
extern std::queue<BGBPacket> sendPackets;

void sendPacket(BGBPacket packet);
BGBPacket updateNetwork();

void packetHandler(int packetID, int readlength);

void enableNetwork();

void disableNetwork();