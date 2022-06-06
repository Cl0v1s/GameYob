#pragma once

// Don't write directly
extern bool nifiEnabled;

struct NIFI {
    unsigned char state;
    unsigned char pairBuffer;
    unsigned char selfBuffer;

    unsigned int clock;
    unsigned int clockDiff;
};

extern NIFI nifi;


void enableNifi();
void disableNifi();
void sendSync1();
int cyclesWithNifi(int cycles);
void applyNifi();