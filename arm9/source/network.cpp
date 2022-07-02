#include <nds.h>
#include <dswifi9.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <queue>
#include "console.h"
#include "network.h"
#include "gbgfx.h"

// https://devkitpro.org/viewtopic.php?t=3026
#define BBL 1400

std::queue<BGBPacket> receivedQueue;
std::queue<BGBPacket> sendQueue;

int sock;
struct sockaddr_in server = { 0 };

void Timer_10ms(void) {
	Wifi_Timer(10);
}

bool init() {
	if(!Wifi_InitDefault(WFC_CONNECT)) {
		iprintf("Failed to connect!");
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    server.sin_addr.s_addr= inet_addr("192.168.43.34");

    sock = socket( AF_INET, SOCK_STREAM, 0 );
    connect(sock,(struct sockaddr *)&server, sizeof(server));

    char gameyob[8];
    recv(sock, gameyob, sizeof(gameyob) - 1, 0);

    gameyob[7] = '\0';
    iprintf(gameyob);
    if(strcmp(gameyob, "GAMEYOB") != 0) {
        printMenuMessage("Unable to reach server");
        return false;
    }

    char nonblock = 1;
    ioctl(sock, FIONBIO, &nonblock);

    return true;
}

void send(BGBPacket packet) {
    sendQueue.push(packet);
}

int slastDsFrameCounter = -1;
bool sendNetwork() {
    if(dsFrameCounter != slastDsFrameCounter) {
        slastDsFrameCounter = dsFrameCounter;
    } else {
        return false;
    }
    if(sendQueue.size() == 0) return false;
    printLog("Sending %d\n", sendQueue.size());
    int size = sendQueue.size() * sizeof(BGBPacket);
    char outgoing_buffer[size];
    int index = 0;
    while(sendQueue.size() > 0) {
        BGBPacket packet = sendQueue.front();
        memcpy(outgoing_buffer+index, &packet, sizeof(BGBPacket));
        index += sizeof(BGBPacket);
        sendQueue.pop();
    }
    send(sock, outgoing_buffer, size, 0);
    return true;
}

int rlastDsFrameCounter = -1;
void receive() {
    if(dsFrameCounter != rlastDsFrameCounter) {
        rlastDsFrameCounter = dsFrameCounter;
    } else {
        return;
    }
    char incoming_buffer[256];
    int received = recv(sock, &incoming_buffer, 256, 0);
    // we wait for at least 1 packet
    if(received < (int)sizeof(BGBPacket)) return;
    printLog("Received %d/%d\n", received, sizeof(BGBPacket));
    int index = 0;
    while(index < received) {
        BGBPacket packet;
        memcpy(&packet, incoming_buffer+index, sizeof(BGBPacket));
        printLog("Got %d %d %d %d %d\n", packet.b1, packet.b2, packet.b3, packet.b4, packet.i1);
        index += sizeof(BGBPacket);
        receivedQueue.push(packet);
        // eventually manage last incomplete packet
        //if(received - index < (int)sizeof(BGBPacket)) index = received;
    }
    printLog("%d packets in queue\n", receivedQueue.size());
}

BGBPacket updateNetwork() {
    if(sendNetwork() == false) receive();
    BGBPacket packet = { 0, 0 ,0 , 0 ,0};
    if(receivedQueue.size() == 0) return packet;
    packet = receivedQueue.front();
    receivedQueue.pop();
    return packet;
}


void stop() {
    closesocket(sock);
    Wifi_DisableWifi();
}