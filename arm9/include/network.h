#pragma once

struct BGBPacket {
    unsigned char b1;
    unsigned char b2;
    unsigned char b3;
    unsigned char b4;
    unsigned int i1;
};


void init();
void send(BGBPacket packet);


BGBPacket receive();

void stop();