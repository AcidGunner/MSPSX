#ifndef PAD_H
#define PAD_H

#include <stdint.h>
#include <psxpad.h>

void Pad_Init(void);
void Pad_Update(void);

uint16_t Pad_Held(void);
uint16_t Pad_Pressed(void);
uint16_t Pad_Released(void);

const char *DebugOutput();

#endif