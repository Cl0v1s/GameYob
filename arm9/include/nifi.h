#pragma once

// Don't write directly
extern bool nifiEnabled;


struct NIFIStruct {
    unsigned char master;
    unsigned char state;
    int delay;
    time_t resetAt;
    int pairTotalCycles;
    unsigned char pairBuffer;
    unsigned char selfBuffer;
    int cyclesSerialTransfer;
};

extern NIFIStruct nifi;

void enableNifi();
void disableNifi();
int updateNifi(int cycles);
void sendSync1();
void sendSync2();
void updateBuffer(unsigned char data);
bool applyTransfer();
void timeout();
void waitTransfer();
void askSerial();