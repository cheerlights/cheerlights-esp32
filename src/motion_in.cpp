// The board's motion sensor: the QMI8658 accelerometer on the Waveshare ESP32-S3-Matrix, read over I2C for the Flow
// effect. Only a board whose wiring is a known preset with a sensor reads it (motionPins), so no pin of a board wired
// by hand is ever driven here.
#include <Arduino.h>
#include <Wire.h>

#include "app.h"

namespace cheerlights {

namespace {

// QMI8658 registers, from QST's datasheet.
constexpr uint8_t REG_WHO_AM_I = 0x00;  // reads 0x05
constexpr uint8_t REG_CTRL1 = 0x02;     // bit 6: register addresses step on through a multi-byte read
constexpr uint8_t REG_CTRL2 = 0x03;     // accelerometer full scale (bits 6..4) and output rate (bits 3..0)
constexpr uint8_t REG_CTRL7 = 0x08;     // bit 0 turns the accelerometer on
constexpr uint8_t REG_AX_L = 0x35;      // x, y, z, each two bytes, low byte first
constexpr uint8_t REG_RESET = 0x60;     // 0xB0 resets the chip
constexpr uint8_t WHO_AM_I = 0x05;
constexpr uint8_t ADDRESSES[] = {0x6B, 0x6A};  // by how the board ties SA0
constexpr float LSB_PER_G = 8192.0f;           // at plus or minus 4 g

uint8_t address = 0;
bool present = false;

bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegisters(uint8_t reg, uint8_t* out, size_t count) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(static_cast<int>(address), static_cast<int>(count)) != static_cast<int>(count)) return false;
  for (size_t i = 0; i < count; i++) out[i] = static_cast<uint8_t>(Wire.read());
  return true;
}

}  // namespace

bool startMotion() {
  int sda = -1, scl = -1;
  if (!motionPins(settings, CL_CHIP, sda, scl)) return false;
  Wire.begin(sda, scl, 400000);
  for (uint8_t a : ADDRESSES) {
    address = a;
    uint8_t id = 0;
    if (readRegisters(REG_WHO_AM_I, &id, 1) && id == WHO_AM_I) {
      present = true;
      break;
    }
  }
  if (!present) {
    logf("motion none\n");
    return false;
  }
  writeRegister(REG_RESET, 0xB0);
  delay(15);
  // Addresses step on, plus or minus 4 g at about 125 readings a second, accelerometer only.
  present = writeRegister(REG_CTRL1, 0x40) && writeRegister(REG_CTRL2, 0x16) && writeRegister(REG_CTRL7, 0x01);
  logf("motion %s address=0x%02x\n", present ? "qmi8658" : "failed", address);
  return present;
}

bool readGravity(float& x, float& y, float& z) {
  if (!present) return false;
  uint8_t b[6];
  if (!readRegisters(REG_AX_L, b, sizeof(b))) return false;
  x = static_cast<int16_t>(b[0] | (b[1] << 8)) / LSB_PER_G;
  y = static_cast<int16_t>(b[2] | (b[3] << 8)) / LSB_PER_G;
  z = static_cast<int16_t>(b[4] | (b[5] << 8)) / LSB_PER_G;
  return true;
}

}  // namespace cheerlights
