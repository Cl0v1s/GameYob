#include <nds.h>
#include <dswifi9.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include "console.h"
#include "network.h"

int sock;
struct sockaddr_in server = { 0 };


bool init() {
	if(!Wifi_InitDefault(WFC_CONNECT)) {
		iprintf("Failed to connect!");
    }

    server.sin_family = AF_INET;
    server.sin_port = htons(8080);
    server.sin_addr.s_addr= inet_addr("192.168.1.34");

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
    // printLog("Sending %d / %d\n", packet.b1, packet.b2);
    send(sock, &packet, sizeof(BGBPacket), 0);
}


BGBPacket receive() {
    BGBPacket packet = { 0, 0, 0, 0, 0};
    int received = recv(sock, &packet, sizeof(BGBPacket), 0);
    if(received == -1) return packet;
    // printLog("Receiving %d / %d %d\n", packet.b1, packet.b2, received);
    return packet;
}


void stop() {
    closesocket(sock);
}