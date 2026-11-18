#include "liteon_psu.h"

LiteonPsu::LiteonPsu(TwoWire &wire, uint8_t address7) : _wire(wire), _address7(address7) {}

void LiteonPsu::setAddress7(uint8_t address7) {
  _address7 = address7;
}

void LiteonPsu::setRespectSequenceSpacing(bool respectSequenceSpacing) {
  _respectSequenceSpacing = respectSequenceSpacing;
}

LiteonResult LiteonPsu::readRaw(LiteonRegister reg, uint16_t &value) {
  if (!isEvenRegister(reg)) {
    return LiteonResult::InvalidRegister;
  }

  const uint8_t regByte = static_cast<uint8_t>(reg);
  const uint8_t writeChecksum = checksumWithWriteAddress(&regByte, 1);

  waitForSequenceWindow();

  _wire.beginTransmission(_address7);
  _wire.write(regByte);
  _wire.write(writeChecksum);
  if (_wire.endTransmission() != 0) {
    markSequenceComplete();
    return LiteonResult::I2cNack;
  }

  if (_wire.requestFrom(_address7, static_cast<uint8_t>(3)) != 3) {
    while (_wire.available() > 0) {
      _wire.read();
    }
    markSequenceComplete();
    return LiteonResult::ShortRead;
  }

  const uint8_t low = _wire.read();
  const uint8_t high = _wire.read();
  const uint8_t receivedChecksum = _wire.read();

  markSequenceComplete();

  if (low == 0x55 && high == 0xAA && receivedChecksum == 0xFE) {
    return LiteonResult::DeviceReportedChecksumError;
  }

  const uint8_t dataBytes[] = {low, high};
  if (checksum(dataBytes, sizeof(dataBytes)) != receivedChecksum) {
    return LiteonResult::ChecksumMismatch;
  }

  value = static_cast<uint16_t>(low) | (static_cast<uint16_t>(high) << 8);
  return LiteonResult::Ok;
}

LiteonResult LiteonPsu::writeRaw(LiteonRegister reg, uint16_t value) {
  if (!isEvenRegister(reg)) {
    return LiteonResult::InvalidRegister;
  }

  const uint8_t writeBytes[] = {
      static_cast<uint8_t>(reg),
      static_cast<uint8_t>(value & 0x00FF),
      static_cast<uint8_t>((value >> 8) & 0x00FF),
  };
  const uint8_t writeChecksum = checksumWithWriteAddress(writeBytes, sizeof(writeBytes));

  waitForSequenceWindow();

  _wire.beginTransmission(_address7);
  _wire.write(writeBytes[0]);
  _wire.write(writeBytes[1]);
  _wire.write(writeBytes[2]);
  _wire.write(writeChecksum);
  if (_wire.endTransmission() != 0) {
    markSequenceComplete();
    return LiteonResult::I2cNack;
  }

  if (_wire.requestFrom(_address7, static_cast<uint8_t>(1)) != 1) {
    while (_wire.available() > 0) {
      _wire.read();
    }
    markSequenceComplete();
    return LiteonResult::ShortRead;
  }

  const uint8_t ack = _wire.read();
  markSequenceComplete();

  if (ack == writeChecksum) {
    return LiteonResult::Ok;
  }
  if (ack == static_cast<uint8_t>(~writeChecksum)) {
    return LiteonResult::DeviceReportedChecksumError;
  }
  return LiteonResult::ChecksumMismatch;
}

LiteonResult LiteonPsu::readStatus(LiteonStatus &status) {
  LiteonResult result = readRaw(LiteonRegister::UcInfo, status.ucInfo);
  if (result != LiteonResult::Ok) return result;

  result = readRaw(LiteonRegister::PsStatus, status.psStatus);
  if (result != LiteonResult::Ok) return result;

  result = readRaw(LiteonRegister::ShutdownEvent, status.shutdownEvent);
  if (result != LiteonResult::Ok) return result;

  result = readRaw(LiteonRegister::WarningEvent, status.warningEvent);
  if (result != LiteonResult::Ok) return result;

  result = readRaw(LiteonRegister::PsControl, status.psControl);
  if (result != LiteonResult::Ok) return result;

  return LiteonResult::Ok;
}

LiteonResult LiteonPsu::readTelemetry(LiteonTelemetry &telemetry) {
  LiteonResult result = readRaw(LiteonRegister::InputVoltage, telemetry.inputVoltageRaw);
  if (result != LiteonResult::Ok) return result;

  result = readRaw(LiteonRegister::InputCurrent, telemetry.inputCurrentRaw);
  if (result != LiteonResult::Ok) return result;

  result = readRaw(LiteonRegister::InputPower, telemetry.inputPowerRaw);
  if (result != LiteonResult::Ok) return result;

  result = readRaw(LiteonRegister::OutputVoltage, telemetry.outputVoltageRaw);
  if (result != LiteonResult::Ok) return result;

  result = readRaw(LiteonRegister::OutputCurrent, telemetry.outputCurrentRaw);
  if (result != LiteonResult::Ok) return result;

  result = readRaw(LiteonRegister::OutputPower, telemetry.outputPowerRaw);
  if (result != LiteonResult::Ok) return result;

  result = readRaw(LiteonRegister::InletTemperature, telemetry.inletTemperatureRaw);
  if (result != LiteonResult::Ok) return result;

  result = readRaw(LiteonRegister::InternalTemperature, telemetry.internalTemperatureRaw);
  if (result != LiteonResult::Ok) return result;

  result = readRaw(LiteonRegister::Fan1Speed, telemetry.fan1SpeedRaw);
  if (result != LiteonResult::Ok) return result;

  telemetry.inputVoltageVrms = scaledSigned(telemetry.inputVoltageRaw, 32.0f);
  telemetry.inputCurrentA = scaledSigned(telemetry.inputCurrentRaw, 64.0f);
  telemetry.inputPowerW = static_cast<float>(telemetry.inputPowerRaw);
  telemetry.outputVoltageV = scaledSigned(telemetry.outputVoltageRaw, 256.0f);
  telemetry.outputCurrentA = scaledSigned(telemetry.outputCurrentRaw, 64.0f);
  telemetry.outputPowerW = static_cast<float>(telemetry.outputPowerRaw);
  telemetry.inletTemperatureC = scaledSigned(telemetry.inletTemperatureRaw, 64.0f);
  telemetry.internalTemperatureC = scaledSigned(telemetry.internalTemperatureRaw, 64.0f);
  telemetry.fan1SpeedRpm = telemetry.fan1SpeedRaw;

  return LiteonResult::Ok;
}

LiteonResult LiteonPsu::clearInterrupt() {
  uint16_t psControl = 0;
  LiteonResult result = readRaw(LiteonRegister::PsControl, psControl);
  if (result != LiteonResult::Ok) {
    return result;
  }

  psControl &= static_cast<uint16_t>(~kPsControlInterruptBit);
  return writeRaw(LiteonRegister::PsControl, psControl);
}

LiteonResult LiteonPsu::resetPeakOutputCurrent() {
  return writeRaw(LiteonRegister::PeakOutputCurrent, 0x0000);
}

LiteonResult LiteonPsu::resetAverageInputPower() {
  LiteonResult result = writeRaw(LiteonRegister::AverageInputPowerCount, 0x0000);
  if (result != LiteonResult::Ok) return result;

  result = writeRaw(LiteonRegister::MaxHalfSecondInputPower, 0x0000);
  if (result != LiteonResult::Ok) return result;

  return writeRaw(LiteonRegister::MaxHalfSecondInputCurrent, 0x0000);
}

void LiteonPsu::waitForSequenceWindow() {
  if (!_respectSequenceSpacing || !_hasSequenceEnd) {
    return;
  }

  const uint32_t elapsedMs = millis() - _lastSequenceEndMs;
  if (elapsedMs < kMinSequenceSpacingMs) {
    delay(kMinSequenceSpacingMs - elapsedMs);
  }
}

void LiteonPsu::markSequenceComplete() {
  _hasSequenceEnd = true;
  _lastSequenceEndMs = millis();
}

uint8_t LiteonPsu::checksumWithWriteAddress(const uint8_t *bytes, uint8_t length) const {
  uint8_t sum = static_cast<uint8_t>(_address7 << 1);
  for (uint8_t i = 0; i < length; ++i) {
    sum = static_cast<uint8_t>(sum + bytes[i]);
  }
  return static_cast<uint8_t>(0U - sum);
}

uint8_t LiteonPsu::checksum(const uint8_t *bytes, uint8_t length) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < length; ++i) {
    sum = static_cast<uint8_t>(sum + bytes[i]);
  }
  return static_cast<uint8_t>(0U - sum);
}

bool LiteonPsu::isEvenRegister(LiteonRegister reg) {
  return (static_cast<uint8_t>(reg) & 0x01) == 0;
}

float LiteonPsu::scaledSigned(uint16_t raw, float divisor) {
  return static_cast<float>(static_cast<int16_t>(raw)) / divisor;
}
