#pragma once

#include <Arduino.h>
#include <Wire.h>

enum class LiteonResult : uint8_t {
  Ok,
  I2cNack,
  ShortRead,
  ChecksumMismatch,
  DeviceReportedChecksumError,
  InvalidRegister,
};

enum class LiteonRegister : uint8_t {
  UcInfo = 0x00,
  PsStatus = 0x02,
  ShutdownEvent = 0x04,
  WarningEvent = 0x06,
  InputVoltage = 0x08,
  InputCurrent = 0x0A,
  InputPower = 0x0C,
  OutputVoltage = 0x0E,
  OutputCurrent = 0x10,
  OutputPower = 0x12,
  InletTemperature = 0x1A,
  InternalTemperature = 0x1C,
  Fan1Speed = 0x1E,
  PsControl = 0x3A,
  PeakOutputCurrent = 0x36,
  AverageInputPowerCount = 0x30,
  MaxHalfSecondInputPower = 0x32,
  MaxHalfSecondInputCurrent = 0x34,
};

struct LiteonStatus {
  uint16_t ucInfo = 0;
  uint16_t psStatus = 0;
  uint16_t shutdownEvent = 0;
  uint16_t warningEvent = 0;
  uint16_t psControl = 0;

  uint8_t majorFirmwareRevision() const { return static_cast<uint8_t>((ucInfo >> 12) & 0x0F); }
  uint8_t minorFirmwareRevision() const { return static_cast<uint8_t>((ucInfo >> 8) & 0x0F); }
  uint8_t ucType() const { return static_cast<uint8_t>((ucInfo >> 4) & 0x0F); }
  bool outputDcOk() const { return (psStatus & (1U << 0)) != 0; }
  bool inputOk() const { return (psStatus & (1U << 1)) != 0; }
  bool psonAsserted() const { return (psStatus & (1U << 2)) != 0; }
  uint8_t vendorId() const { return static_cast<uint8_t>((psStatus >> 3) & 0x07); }
  uint8_t inputRange() const { return static_cast<uint8_t>((psStatus >> 8) & 0x07); }
  bool interruptLatched() const { return (psControl & (1U << 5)) != 0; }
};

struct LiteonTelemetry {
  uint16_t inputVoltageRaw = 0;
  uint16_t inputCurrentRaw = 0;
  uint16_t inputPowerRaw = 0;
  uint16_t outputVoltageRaw = 0;
  uint16_t outputCurrentRaw = 0;
  uint16_t outputPowerRaw = 0;
  uint16_t inletTemperatureRaw = 0;
  uint16_t internalTemperatureRaw = 0;
  uint16_t fan1SpeedRaw = 0;

  float inputVoltageVrms = 0.0f;
  float inputCurrentA = 0.0f;
  float inputPowerW = 0.0f;
  float outputVoltageV = 0.0f;
  float outputCurrentA = 0.0f;
  float outputPowerW = 0.0f;
  float inletTemperatureC = 0.0f;
  float internalTemperatureC = 0.0f;
  uint16_t fan1SpeedRpm = 0;
};

class LiteonPsu {
 public:
  explicit LiteonPsu(TwoWire &wire, uint8_t address7 = monitoringAddress7(0));

  static constexpr uint8_t monitoringAddress7(uint8_t addPins) {
    return static_cast<uint8_t>(0x58 + (addPins & 0x07));
  }

  uint8_t address7() const { return _address7; }
  void setAddress7(uint8_t address7);
  bool respectSequenceSpacing() const { return _respectSequenceSpacing; }
  void setRespectSequenceSpacing(bool respectSequenceSpacing);

  LiteonResult readRaw(LiteonRegister reg, uint16_t &value);
  LiteonResult writeRaw(LiteonRegister reg, uint16_t value);
  LiteonResult readStatus(LiteonStatus &status);
  LiteonResult readTelemetry(LiteonTelemetry &telemetry);
  LiteonResult clearInterrupt();
  LiteonResult resetPeakOutputCurrent();
  LiteonResult resetAverageInputPower();

 private:
  static constexpr uint32_t kMinSequenceSpacingMs = 80;
  static constexpr uint16_t kPsControlInterruptBit = 1U << 5;

  TwoWire &_wire;
  uint8_t _address7;
  bool _hasSequenceEnd = false;
  uint32_t _lastSequenceEndMs = 0;
  bool _respectSequenceSpacing = true;

  void waitForSequenceWindow();
  void markSequenceComplete();
  uint8_t checksumWithWriteAddress(const uint8_t *bytes, uint8_t length) const;
  static uint8_t checksum(const uint8_t *bytes, uint8_t length);
  static bool isEvenRegister(LiteonRegister reg);
  static float scaledSigned(uint16_t raw, float divisor);
};
