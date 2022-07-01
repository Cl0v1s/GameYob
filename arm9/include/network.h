#pragma once

#include <queue>

#define NULL_PACKET { 0, 0, 0, 0 ,0}

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
BGBPacket receivePacket();

void packetHandler(int packetID, int readlength);

void enableNetwork();

void disableNetwork();