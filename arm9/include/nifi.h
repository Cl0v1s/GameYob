#pragma once

// Don't write directly
extern bool nifiEnabled;

void enableNifi();
void disableNifi();
bool updateNifi();
void sendSync1();
void applyTransfer();
void timeout();