#ifndef CALC_CRC_A_N_B
#define CALC_CRC_A_N_B

#define CRC_A 1
#define CRC_B 2
#define BYTE unsigned char

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define CRC_A 1
#define CRC_B 2
#define BYTE unsigned char

unsigned short UpdateCrc(unsigned char ch, unsigned short *lpwCrc);
void ComputeCrc(int CRCType, char *Data, int Length, BYTE *TransmitFirst, BYTE *TransmitSecond);

#endif
