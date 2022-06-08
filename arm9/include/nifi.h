#pragma once

// Don't write directly
extern bool nifiEnabled;

struct NIFI {
    unsigned char type;
    unsigned char state;
    unsigned char pairBuffer;
    unsigned char selfBuffer;

    unsigned int cycles;
    unsigned int pairCycles;
    unsigned int cylesToSerialTransfer;
};

extern NIFI nifi;


void enableNifi();
void disableNifi();
void sendSync1(int when);
void cyclesWithNifi();
void applyNifi();
void waitForNifi();