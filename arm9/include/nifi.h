#pragma once

// Don't write directly
extern bool nifiEnabled;

void enableNifi();
void disableNifi();
bool updateNifi();
void sendSync1();
void sendSync2();
void updateBuffer(unsigned char data);
void applyTransfer();
void timeout();
void waitTransfer();