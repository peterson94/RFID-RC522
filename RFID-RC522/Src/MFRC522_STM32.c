#include "MFRC522_STM32.h"
#include "main.h"
#include "calculation_crc.h"

uint8_t atqa[2];

void FIFO_ADD(FIFO_64B * FIFO, uint8_t C) {
    uint8_t index = FIFO->head;

    if (index == FIFO->tail){
        FIFO->head = 0;
    }

    FIFO->buffer[index] = C;
    FIFO->head += 1;
}

void MFRC522_Init(MFRC522_t *dev) {
    USER_LOG("MFRC522 Min Init started");
    // Hardware reset
    HAL_GPIO_WritePin(dev->rstPort, dev->rstPin, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(dev->rstPort, dev->rstPin, GPIO_PIN_SET);
    HAL_Delay(50);

    // Soft reset
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_SoftReset);
    HAL_Delay(50);

    // Clear interrupts
    MFRC522_WriteReg(dev, PCD_ComIrqReg, 0x7F);

    // Flush FIFO
    MFRC522_WriteReg(dev, PCD_FIFOLevelReg, 0x80);

    // Timer: ~25ms timeout
    MFRC522_WriteReg(dev, PCD_TModeReg, 0x80);      // Timer starts immediately
    MFRC522_WriteReg(dev, PCD_TPrescalerReg, 0xA9); // 80kHz clock
    MFRC522_WriteReg(dev, PCD_TReloadRegH, 0x03);   // 1000 ticks = ~12.5ms
    MFRC522_WriteReg(dev, PCD_TReloadRegL, 0xE8);

    // RF settings
    MFRC522_WriteReg(dev, PCD_TxAutoReg, 0x40);     // 100% ASK modulation
    MFRC522_WriteReg(dev, PCD_RFCfgReg, 0x7F);      // Max gain (48dB)
    MFRC522_WriteReg(dev, PCD_DemodReg, 0x4D);      // Sensitivity for clones

    // Enable antenna
    MFRC522_AntennaOn(dev);
    HAL_Delay(10);  // Let RF stabilize

    uint8_t version = MFRC522_ReadReg(dev, PCD_VersionReg);
    if ((version != 0x91)||(version != 0x92)){
    	USER_LOG("Version: 0x%02X (counterfeit OK for UID)", version);
    }
    else USER_LOG("Version: 0x%02X", version);
    uint8_t txCtrl = MFRC522_ReadReg(dev, PCD_TxControlReg);
    DEBUG_LOG("TxControlReg: 0x%02X (expect >= 0x03)", txCtrl);
    USER_LOG("MFRC522 Min Init complete");
}

void MFRC522_AntennaOff(MFRC522_t *dev) {
    MFRC522_ClearBitMask(dev, PCD_TxControlReg, 0x03);
    DEBUG_LOG("Antenna off");
}

void MFRC522_AntennaOn(MFRC522_t *dev) {
    MFRC522_SetBitMask(dev, PCD_TxControlReg, 0x03);
    DEBUG_LOG("Antenna on");
}

uint8_t MFRC522_ReadReg(MFRC522_t *dev, uint8_t reg) {
    uint8_t addr = ((reg << 1) & 0x7E) | 0x80;
    uint8_t val = 0;
    HAL_GPIO_WritePin(dev->csPort, dev->csPin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(dev->hspi, &addr, 1, HAL_MAX_DELAY);
    HAL_SPI_Receive(dev->hspi, &val, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(dev->csPort, dev->csPin, GPIO_PIN_SET);
    HAL_Delay(1);
    DEBUG_LOG("ReadReg: 0x%02X -> 0x%02X", reg, val);
    return val;
}

void MFRC522_WriteReg(MFRC522_t *dev, uint8_t reg, uint8_t value) {
    uint8_t addr = (reg << 1) & 0x7E;
    HAL_GPIO_WritePin(dev->csPort, dev->csPin, GPIO_PIN_RESET);
    HAL_SPI_Transmit(dev->hspi, &addr, 1, HAL_MAX_DELAY);
    HAL_SPI_Transmit(dev->hspi, &value, 1, HAL_MAX_DELAY);
    HAL_GPIO_WritePin(dev->csPort, dev->csPin, GPIO_PIN_SET);
    HAL_Delay(1);
    DEBUG_LOG("WriteReg: 0x%02X = 0x%02X", reg, value);
}

void MFRC522_SetBitMask(MFRC522_t *dev, uint8_t reg, uint8_t mask) {
    uint8_t tmp = MFRC522_ReadReg(dev, reg);
    MFRC522_WriteReg(dev, reg, tmp | mask);
    DEBUG_LOG("SetBitMask: 0x%02X |= 0x%02X", reg, mask);
}

void MFRC522_ClearBitMask(MFRC522_t *dev, uint8_t reg, uint8_t mask) {
    uint8_t tmp = MFRC522_ReadReg(dev, reg);
    MFRC522_WriteReg(dev, reg, tmp & (~mask));
    DEBUG_LOG("ClearBitMask: 0x%02X &= ~0x%02X", reg, mask);
}

uint8_t MFRC522_RequestA(MFRC522_t *dev, uint8_t *atqa) {
    DEBUG_LOG("RequestA");
    MFRC522_AntennaOff(dev);  // Reset RF
    HAL_Delay(5);  // Allow chip to stabilize
    MFRC522_AntennaOn(dev);
    HAL_Delay(5);  // Ensure RF is ready
    MFRC522_WriteReg(dev, PCD_ComIrqReg, 0x7F);      // Clear IRQs
    MFRC522_WriteReg(dev, PCD_FIFOLevelReg, 0x80);   // Flush FIFO
    MFRC522_WriteReg(dev, PCD_BitFramingReg, 0x07);  // 7 bits for REQA
    MFRC522_WriteReg(dev, PCD_FIFODataReg, PICC_REQA);
    HAL_Delay(2);  // Increased for counterfeit chip stability
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Transceive);
    MFRC522_SetBitMask(dev, PCD_BitFramingReg, 0x80);

    // Poll for completion (25ms timeout)
    uint32_t timeout = HAL_GetTick() + 25;
    while (HAL_GetTick() < timeout) {
        uint8_t status2 = MFRC522_ReadReg(dev, PCD_Status2Reg);
        if (status2 & 0x01) {  // Command complete
            uint8_t err = MFRC522_ReadReg(dev, PCD_ErrorReg);
            if (err & 0x1D) {  // Protocol/parity/buffer errors
                DEBUG_LOG("RequestA error: 0x%02X", err);
                MFRC522_AntennaOff(dev);
                HAL_Delay(5);
                MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle); // Stop command
                return STATUS_ERROR;
            }
            uint8_t fifoLvl = MFRC522_ReadReg(dev, PCD_FIFOLevelReg);
            if (fifoLvl >= 2) {  // ATQA is 2 bytes
                atqa[0] = MFRC522_ReadReg(dev, PCD_FIFODataReg);
                atqa[1] = MFRC522_ReadReg(dev, PCD_FIFODataReg);
                DEBUG_LOG("RequestA ATQA: 0x%02X 0x%02X", atqa[0], atqa[1]);
                MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle); // Stop command
                HAL_Delay(2);  // Post-command delay
                return STATUS_OK;
            }
            DEBUG_LOG("RequestA bad FIFO level: %d", fifoLvl);
            MFRC522_AntennaOff(dev);
            HAL_Delay(5);
            MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
            return STATUS_ERROR;
        }
        HAL_Delay(1);  // Mimic debug log timing
    }
    DEBUG_LOG("RequestA timeout");
    MFRC522_AntennaOff(dev);
    HAL_Delay(5);
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
    return STATUS_TIMEOUT;
}

uint8_t MFRC522_Anticoll(MFRC522_t *dev, uint8_t *uid) {  // Returns 4-byte UID + BCC
    DEBUG_LOG("Anticoll");
    MFRC522_WriteReg(dev, PCD_ComIrqReg, 0x7F);      // Clear IRQs
    MFRC522_WriteReg(dev, PCD_FIFOLevelReg, 0x80);   // Flush FIFO
    MFRC522_WriteReg(dev, PCD_BitFramingReg, 0x00);  // Full frame

    MFRC522_WriteReg(dev, PCD_FIFODataReg, PICC_SEL_CL1);  // 0x93
    MFRC522_WriteReg(dev, PCD_FIFODataReg, 0x20);    // Fixed CRC

    HAL_Delay(2);  // Delay for stability
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Transceive);
    MFRC522_SetBitMask(dev, PCD_BitFramingReg, 0x80);

    uint32_t timeout = HAL_GetTick() + 25;
    while (HAL_GetTick() < timeout) {
        uint8_t status2 = MFRC522_ReadReg(dev, PCD_Status2Reg);
        if (status2 & 0x01) {  // Command complete
            uint8_t err = MFRC522_ReadReg(dev, PCD_ErrorReg);
            if (err & 0x1D) {
                DEBUG_LOG("Anticoll error: 0x%02X", err);
                MFRC522_AntennaOff(dev);
                HAL_Delay(5);
                MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
                return STATUS_ERROR;
            }
            uint8_t fifoLvl = MFRC522_ReadReg(dev, PCD_FIFOLevelReg);
            if (fifoLvl == 5) {  // 4-byte UID + BCC
                for (int i = 0; i < 5; i++) {
                    uid[i] = MFRC522_ReadReg(dev, PCD_FIFODataReg);
                }
                // Validate BCC
                uint8_t calcBcc = uid[0] ^ uid[1] ^ uid[2] ^ uid[3];
                if (uid[4] != calcBcc) {
                    DEBUG_LOG("Anticoll bad BCC: calc=0x%02X, got=0x%02X", calcBcc, uid[4]);
                    MFRC522_AntennaOff(dev);
                    HAL_Delay(5);
                    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
                    return STATUS_ERROR;
                }
                DEBUG_LOG("Anticoll UID: %02X %02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3], uid[4]);
                MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
                HAL_Delay(2);  // Post-command delay
                return STATUS_OK;
            }
            DEBUG_LOG("Anticoll bad FIFO level: %d", fifoLvl);
            MFRC522_AntennaOff(dev);
            HAL_Delay(5);
            MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
            return STATUS_ERROR;
        }
        HAL_Delay(1);  // Mimic debug log timing
    }
    DEBUG_LOG("Anticoll timeout");
    MFRC522_AntennaOff(dev);
    HAL_Delay(5);
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
    return STATUS_TIMEOUT;
}

uint8_t MFRC522_Select(MFRC522_t *dev, uint8_t *uid) {  // Returns 4-byte UID + BCC
    DEBUG_LOG("Select");
    MFRC522_WriteReg(dev, PCD_ComIrqReg, 0x7F);      // Clear IRQs
    MFRC522_WriteReg(dev, PCD_FIFOLevelReg, 0x80);   // Flush FIFO
    MFRC522_WriteReg(dev, PCD_BitFramingReg, 0x00);  // Full frame

	FIFO_64B TEMP = {{},0,0};
	FIFO_ADD(&TEMP,PICC_SEL_CL1);
	FIFO_ADD(&TEMP,0x70);
	FIFO_ADD(&TEMP,uid[0]);
	FIFO_ADD(&TEMP,uid[1]);
	FIFO_ADD(&TEMP,uid[2]);
	FIFO_ADD(&TEMP,uid[3]);
	FIFO_ADD(&TEMP,uid[0]^uid[1]^uid[2]^uid[3]);

	uint8_t A, B;
	ComputeCrc(CRC_A, TEMP.buffer, TEMP.head, &A, &B);
	FIFO_ADD(&TEMP,A);
	FIFO_ADD(&TEMP,B);

	for (int i = 0; i < TEMP.head; i++){
		MFRC522_WriteReg(dev, PCD_FIFODataReg, TEMP.buffer[i]);
	}

    HAL_Delay(2);  // Delay for stability
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Transceive);
    MFRC522_SetBitMask(dev, PCD_BitFramingReg, 0x80);

    return STATUS_OK;
}

uint8_t MFRC522_Authentication(MFRC522_t *dev, uint8_t *uid, uint8_t address) {  // Output: block_data[16]
	DEBUG_LOG("Authenticate");
	MFRC522_WriteReg(dev, PCD_ComIrqReg, 0x7F);      // Clear IRQs
	MFRC522_WriteReg(dev, PCD_FIFOLevelReg, 0x80);   // Flush FIFO
	MFRC522_WriteReg(dev, PCD_BitFramingReg, 0x00);  // Full frame

	FIFO_64B TEMP = {{},0,0};
	FIFO_ADD(&TEMP, PICC_AUTH_B);
	FIFO_ADD(&TEMP, address);
	FIFO_ADD(&TEMP, 0xFF);
	FIFO_ADD(&TEMP, 0xFF);
	FIFO_ADD(&TEMP, 0xFF);
	FIFO_ADD(&TEMP, 0xFF);
	FIFO_ADD(&TEMP, 0xFF);
	FIFO_ADD(&TEMP, 0xFF);
	FIFO_ADD(&TEMP, uid[0]);
	FIFO_ADD(&TEMP, uid[1]);
	FIFO_ADD(&TEMP, uid[2]);
	FIFO_ADD(&TEMP, uid[3]);

	for (int i = 0; i < TEMP.head; i++){
		MFRC522_WriteReg(dev, PCD_FIFODataReg, TEMP.buffer[i]);
	}

	HAL_Delay(2);  // Delay for stability
	MFRC522_WriteReg(dev, PCD_CommandReg, PCD_MFAuthent);

    uint32_t timeout = HAL_GetTick() + 25;

    while (HAL_GetTick() < timeout){
    	uint8_t irq = MFRC522_ReadReg(dev, PCD_ComIrqReg);
        uint8_t status2 = MFRC522_ReadReg(dev, PCD_Status2Reg);

        if (irq & 0x02){
        	return STATUS_ERROR;
        }

        if (irq & 0x10){
			if (status2 & 0x08){  // MFCrypto1On bit is set
				uint8_t err = MFRC522_ReadReg(dev, PCD_ErrorReg);
				if (err & 0x1D){
					DEBUG_LOG("Authentication error: 0x%02X", err);
					MFRC522_AntennaOff(dev);
					HAL_Delay(5);
					MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
					return STATUS_ERROR;
				}

				DEBUG_LOG("Authentication successful.");
				MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
				HAL_Delay(2);  // Post-command delay
				return STATUS_OK;
			}

			HAL_Delay(1);  // Mimic debug log timing
        }
    }
    USER_LOG("Authentication timeout");
    MFRC522_AntennaOff(dev);
    HAL_Delay(5);
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
    return STATUS_TIMEOUT;
}

uint8_t MFRC522_Read_Block(MFRC522_t *dev, uint8_t address, uint8_t *block_data, size_t block_length) {  // Returns 4-byte UID + BCC
	DEBUG_LOG("Read block");
	MFRC522_WriteReg(dev, PCD_ComIrqReg, 0x7F);      // Clear IRQs
	MFRC522_WriteReg(dev, PCD_FIFOLevelReg, 0x80);   // Flush FIFO
	MFRC522_WriteReg(dev, PCD_BitFramingReg, 0x00);  // Full frame
	MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle); // Idle state

	FIFO_64B TEMP = {{},0,0};
	FIFO_ADD(&TEMP, PICC_READ);
	FIFO_ADD(&TEMP, address);

	uint8_t A, B;
	ComputeCrc(CRC_A, TEMP.buffer, TEMP.head, &A, &B);
	FIFO_ADD(&TEMP, A);
	FIFO_ADD(&TEMP, B);

	for (int i = 0; i < TEMP.head; i++){
		MFRC522_WriteReg(dev, PCD_FIFODataReg, TEMP.buffer[i]);
	}

    HAL_Delay(2);  // Delay for stability
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Transceive);
    MFRC522_SetBitMask(dev, PCD_BitFramingReg, 0x80);

	for (int i = 0; i < block_length; i++){
		block_data[i] = MFRC522_ReadReg(dev, PCD_FIFODataReg);
	}

	return STATUS_OK;
}

uint8_t MFRC522_Write_Block(MFRC522_t *dev, uint8_t address, uint8_t *block_data, size_t block_length) {
	DEBUG_LOG("Write block");
	MFRC522_WriteReg(dev, PCD_ComIrqReg, 0x7F);      // Clear IRQs
	MFRC522_WriteReg(dev, PCD_FIFOLevelReg, 0x80);   // Flush FIFO
	MFRC522_WriteReg(dev, PCD_BitFramingReg, 0x00);  // Full frame
	MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle); // Idle state

	FIFO_64B TEMP = {{},0,0};
	FIFO_ADD(&TEMP, PICC_WRITE);
	FIFO_ADD(&TEMP, address);

	uint8_t A, B;
	ComputeCrc(CRC_A, TEMP.buffer, TEMP.head, &A, &B);
	FIFO_ADD(&TEMP, A);
	FIFO_ADD(&TEMP, B);

	for (int i = 0; i < TEMP.head; i++){
		MFRC522_WriteReg(dev, PCD_FIFODataReg, TEMP.buffer[i]);
	}

    HAL_Delay(2);  // Delay for stability
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Transceive);
    MFRC522_SetBitMask(dev, PCD_BitFramingReg, 0x80);

    uint32_t timeout = HAL_GetTick() + 25;

    while (HAL_GetTick() < timeout) {
        if (MFRC522_ReadReg(dev, PCD_FIFODataReg) == 0x0A){  // ACK received
        	MFRC522_WriteReg(dev, PCD_ComIrqReg, 0x7F);      // Clear IRQs
        	MFRC522_WriteReg(dev, PCD_FIFOLevelReg, 0x80);   // Flush FIFO

        	TEMP.head = 0;
        	for (int i = 0; i < block_length; i++){ //Upload temporary FIFO with the content to write
        		FIFO_ADD(&TEMP, block_data[i]);
        	}
        	ComputeCrc(CRC_A, TEMP.buffer, TEMP.head, &A, &B);
        	FIFO_ADD(&TEMP, A);
        	FIFO_ADD(&TEMP, B);

        	// Send content to write
        	for (int i = 0; i < TEMP.head; i++){
        		MFRC522_WriteReg(dev, PCD_FIFODataReg, TEMP.buffer[i]);
        	}
            HAL_Delay(2);  // Delay for stability
            MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Transceive);
            MFRC522_SetBitMask(dev, PCD_BitFramingReg, 0x80);

            while (MFRC522_ReadReg(dev, PCD_FIFODataReg) != 0x0A){
            	// warning: it can be infinite loop.
            }
            return STATUS_OK;
        }
    }
    DEBUG_LOG("Writing timeout");
    MFRC522_AntennaOff(dev);
    HAL_Delay(5);
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);
    return STATUS_TIMEOUT;
}

uint8_t MFRC522_ReadUid(MFRC522_t *dev, uint8_t *uid) {  // Output: uid[4]
    DEBUG_LOG("Reading UID");
    // Card detected, read UID
    uint8_t rawUid[5];
    if (MFRC522_Anticoll(dev, rawUid) != STATUS_OK){
    	DEBUG_LOG("Anticollision failed");
        return STATUS_ERROR;
    }

    // Copy UID (drop BCC)
    for (int i = 0; i < 4; i++){
        uid[i] = rawUid[i];
    }

    DEBUG_LOG("Card UID: %02X %02X %02X %02X", uid[0], uid[1], uid[2], uid[3]);
    return STATUS_OK;
}

uint8_t waitcardRemoval (MFRC522_t *dev){
    MFRC522_WriteReg(dev, PCD_Status2Reg, 0x00);
    MFRC522_AntennaOff(dev);
    HAL_Delay(5);
    MFRC522_WriteReg(dev, PCD_CommandReg, PCD_Idle);

    USER_LOG("Waiting for card removal...");
    while (1){
        if (MFRC522_RequestA(dev, atqa) != STATUS_OK){
        	USER_LOG("Card removed");
            return STATUS_OK; // Card removed, return success
        }

        HAL_Delay(100); // Poll every 100ms to check if card is still present
    }
}

uint8_t waitcardDetect (MFRC522_t *dev) {
	atqa[0] = atqa[1] = 0;
	USER_LOG("Waiting for the card...");
	while (1){
	    if (MFRC522_RequestA(dev, atqa) == STATUS_OK){
	    	USER_LOG("Card detected");
	        return STATUS_OK;
	    }

	    HAL_Delay(100);	// Poll every 100ms to check if card is  present
	}
}


