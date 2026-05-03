#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#define CRC_A 1
#define CRC_B 2
#define BYTE unsigned char

unsigned short UpdateCrc(uint8_t ch, uint16_t *lpwCrc){
	ch = (ch^(uint8_t)((*lpwCrc) & 0x00FF));
	ch = (ch^(ch<<4));
	*lpwCrc = (*lpwCrc >> 8)^((uint16_t)ch << 8)^((uint16_t)ch<<3)^((uint16_t)ch>>4);
	return(*lpwCrc);
}

void ComputeCrc(int CRCType, uint8_t *Data, uint8_t Length, uint8_t *TransmitFirst, uint8_t *TransmitSecond){
	uint8_t chBlock;
	unsigned short wCrc;

	switch(CRCType) {
		case CRC_A:
			wCrc = 0x6363; /* ITU-V.41 */
			break;

		case CRC_B:
			wCrc = 0xFFFF; /* ISO/IEC 13239 (formerly ISO/IEC 3309) */
			break;

		default:
			return;
	}

	do {
		chBlock = *Data++;
		UpdateCrc(chBlock, &wCrc);
	} while (--Length);

	if (CRCType == CRC_B)
		wCrc = ~wCrc; /* ISO/IEC 13239 (formerly ISO/IEC 3309) */

	*TransmitFirst = (uint8_t) (wCrc & 0xFF);
	*TransmitSecond = (uint8_t) ((wCrc >> 8) & 0xFF);
}
