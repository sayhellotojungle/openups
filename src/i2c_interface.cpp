#include "i2c_interface.h"

I2CInterface i2cInterface;

I2CInterface::I2CInterface() : i2cBus(&Wire), initialized(false) {
}

bool I2CInterface::checkInitialized() {
    if (!initialized) {
        Serial.println("I2C Interface not initialized");
        return false;
    }
    return true;
}

bool I2CInterface::initialize(int sdaPin, int sclPin, uint32_t frequency) {
    i2cBus->begin(sdaPin, sclPin, frequency);
    initialized = true;
    
    Serial.println("I2C Interface initialized");
    Serial.printf("I2C initialized with SDA: GPIO%d, SCL: GPIO%d, Frequency: %d Hz\n", sdaPin, sclPin, frequency);
    
    return true;
}

bool I2CInterface::isDeviceConnected(uint8_t deviceAddr) {
    if (!checkInitialized()) return false;
    
    i2cBus->beginTransmission(deviceAddr);
    return i2cBus->endTransmission() == 0;
}

void I2CInterface::scanI2CBus() {
    if (!checkInitialized()) return;
    
    Serial.println("\nScanning I2C bus...");
    
    int nDevices = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        i2cBus->beginTransmission(addr);
        if (i2cBus->endTransmission() == 0) {
            Serial.printf("Device found at 0x%02X\n", addr);
            nDevices++;
        }
    }
    
    Serial.printf("Scan complete. Found %d device(s).\n", nDevices);
}

bool I2CInterface::readRegisterByte(uint8_t deviceAddr, uint8_t reg, uint8_t *value) {
    if (!checkInitialized()) return false;
    if (!isDeviceConnected(deviceAddr)) return false;
    
    i2cBus->beginTransmission(deviceAddr);
    i2cBus->write(reg);
    i2cBus->endTransmission(false);
    
    i2cBus->requestFrom(deviceAddr, (uint8_t)1);
    if (i2cBus->available() != 1) return false;
    
    *value = i2cBus->read();
    return true;
}

bool I2CInterface::writeRegisterByte(uint8_t deviceAddr, uint8_t reg, uint8_t value) {
    if (!checkInitialized()) return false;
    if (!isDeviceConnected(deviceAddr)) return false;
    
    i2cBus->beginTransmission(deviceAddr);
    i2cBus->write(reg);
    i2cBus->write(value);
    return i2cBus->endTransmission() == 0;
}

bool I2CInterface::readRegisterWord(uint8_t deviceAddr, uint8_t reg, uint16_t *value) {
    if (!checkInitialized()) return false;
    if (!isDeviceConnected(deviceAddr)) return false;
    
    i2cBus->beginTransmission(deviceAddr);
    i2cBus->write(reg);
    i2cBus->endTransmission(false);
    
    i2cBus->requestFrom(deviceAddr, (uint8_t)2);
    if (i2cBus->available() != 2) return false;
    
    uint8_t lsb = i2cBus->read();
    uint8_t msb = i2cBus->read();
    *value = (msb << 8) | lsb;
    return true;
}

bool I2CInterface::writeRegisterWord(uint8_t deviceAddr, uint8_t reg, uint16_t value) {
    if (!checkInitialized()) return false;
    if (!isDeviceConnected(deviceAddr)) return false;
    
    i2cBus->beginTransmission(deviceAddr);
    i2cBus->write(reg);
    i2cBus->write(value & 0xFF);        // LSB
    i2cBus->write((value >> 8) & 0xFF);  // MSB
    return i2cBus->endTransmission() == 0;
}

uint8_t I2CInterface::CRC8(const uint8_t *ptr, uint8_t len) {
    uint8_t crc = 0;
    while (len--) {
        crc ^= *ptr++;
        for (uint8_t i = 0; i < 8; i++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1);
        }
    }
    return crc;
}

bool I2CInterface::readRegisterByteCRC(uint8_t deviceAddr, uint8_t reg, uint8_t *value) {
    if (!checkInitialized()) return false;
    if (!isDeviceConnected(deviceAddr)) return false;
    
    i2cBus->beginTransmission(deviceAddr);
    i2cBus->write(reg);
    i2cBus->endTransmission(false);
    
    i2cBus->requestFrom(deviceAddr, (uint8_t)2);
    if (i2cBus->available() != 2) return false;
    
    uint8_t data = i2cBus->read();
    uint8_t crc = i2cBus->read();
    
    uint8_t buf[2] = { (uint8_t)((deviceAddr << 1) | 0x01), data };
    if (CRC8(buf, 2) != crc) {
        Serial.printf("CRC failed: device 0x%02X, reg 0x%02X\n", deviceAddr, reg);
        return false;
    }
    
    *value = data;
    return true;
}
bool I2CInterface::readRegisterWordCRC(uint8_t deviceAddr, uint8_t reg, uint16_t *value) {
    if (!checkInitialized()) return false;
    if (!isDeviceConnected(deviceAddr)) return false;
    
    i2cBus->beginTransmission(deviceAddr);
    i2cBus->write(reg);
    i2cBus->endTransmission(false);
    
    i2cBus->requestFrom(deviceAddr, (uint8_t)4);
    if (i2cBus->available() != 4) return false;
    
    uint8_t dataH = i2cBus->read();
    uint8_t crcH = i2cBus->read();
    uint8_t dataL = i2cBus->read();
    uint8_t crcL = i2cBus->read();
    
    uint8_t readAddr = (deviceAddr << 1) | 0x01;
    uint8_t bufH[2] = { readAddr, dataH };
    if (CRC8(bufH, 2) != crcH) {
        Serial.println("CRC failed: High Byte");
        return false;
    }
    
    uint8_t bufL[1] = { dataL };
    if (CRC8(bufL, 1) != crcL) {
        Serial.println("CRC failed: Low Byte");
        return false;
    }
    
    *value = ((uint16_t)dataH << 8) | dataL;
    return true;
}


bool I2CInterface::writeRegisterByteCRC(uint8_t deviceAddr, uint8_t reg, uint8_t value) {
    if (!checkInitialized()) return false;
    if (!isDeviceConnected(deviceAddr)) return false;
    
    uint8_t buf[3] = { (uint8_t)(deviceAddr << 1), reg, value };
    uint8_t crc = CRC8(buf, 3);
    
    i2cBus->beginTransmission(deviceAddr);
    i2cBus->write(reg);
    i2cBus->write(value);
    i2cBus->write(crc);
    return i2cBus->endTransmission() == 0;
}
