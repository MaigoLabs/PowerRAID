import {
  type PsuPins,
  type PsuStatus,
  type PsuTelemetry,
  type RegisterSelector,
  type RegisterValue,
  PsuBackendError,
} from './types'

export type RegisterOption = {
  value: string
  label: string
  address: number
  access: 'RO' | 'RW'
  description: string
  help: RegisterHelp
}

export type RegisterHelp = {
  source: string
  summary: string
  format?: string
  defaultValue?: string
  bitFields?: RegisterHelpBit[]
  readBehavior?: string[]
  writeBehavior?: string[]
  legalValues?: string[]
  notes?: string[]
}

export type RegisterHelpBit = {
  bits: string
  name: string
  description: string
}

export const REGISTER_OPTIONS: RegisterOption[] = [
  {
    value: 'ucinfo',
    label: 'uC info',
    address: 0x00,
    access: 'RO',
    description: 'Firmware revision and monitor microcontroller type bit field.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.1.1',
      summary: 'Identifies the PSU monitor firmware revision and the microcontroller vendor/type code.',
      format: '16-bit read-only bit field.',
      bitFields: [
        { bits: '15-12', name: 'Major firmware revision', description: 'Product release is specified as 2.0.' },
        { bits: '11-8', name: 'Minor firmware revision', description: 'Vendor code revision after product release.' },
        {
          bits: '7-4',
          name: 'uC type',
          description: '0 Microchip, 1 STMicroelectronics, 2 Freescale, 3 Texas Instruments, 4 Silicon Labs; others reserved.',
        },
        { bits: '3-0', name: 'Reserved', description: 'Read as zero.' },
      ],
    },
  },
  {
    value: 'ps-status',
    label: 'PS status',
    address: 0x02,
    access: 'RO',
    description: 'Live PSU state: 12 V DCOK, input OK, PSON, vendor, input range, fan override.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.2.1',
      summary: 'Reports whether the input and main 12 V output are valid, whether PSON is asserted, the vendor code, input range, and whether PSU thermal control overrode a requested fan speed.',
      format: '16-bit read-only bit field.',
      bitFields: [
        { bits: '15-11', name: 'Reserved', description: 'Read as zero.' },
        {
          bits: '10-8',
          name: 'Input range',
          description: '0 out of range, 1 low line 90-108 VAC, 2 mid line 109-132 VAC, 3 high line 180-264 VAC, 4 36-72 VDC, 5 120-420 VDC; others reserved.',
        },
        { bits: '7', name: 'Fan2 override', description: 'Fan2 request has been overridden by internal thermal control; read as zero if fan 2 is absent.' },
        { bits: '6', name: 'Fan1 override', description: 'Fan1 request has been overridden by internal thermal control.' },
        { bits: '5-3', name: 'Vendor ID', description: '010b identifies LiteON; other values are reserved.' },
        { bits: '2', name: 'PSON', description: '1 means PSON is asserted and the PSU is commanded on; 0 means standby.' },
        { bits: '1', name: 'Input OK', description: '1 means AC or DC input is within the PSU valid range.' },
        { bits: '0', name: 'Output DCOK', description: '1 means the main 12 V output is OK.' },
      ],
      notes: ['The datasheet requires hysteresis near input-range boundaries so range and Input OK do not chatter.'],
    },
  },
  {
    value: 'shutdown',
    label: 'Shutdown event',
    address: 0x04,
    access: 'RO',
    description: 'Latched shutdown/failure causes, including over-voltage, over-current, thermal, fan, and input-loss events.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.4.1',
      summary: 'Fault word for shutdown causes. Nonzero bits identify why the PSU shut down or reported a failure.',
      format: '16-bit read-only bit field.',
      bitFields: [
        { bits: '15-13', name: 'Reserved', description: 'Read as zero.' },
        { bits: '12', name: 'OCP2 shutdown', description: 'Shutdown caused by software over-current-protection set point 2.' },
        { bits: '11', name: 'OCP1 shutdown', description: 'Shutdown caused by software over-current-protection set point 1.' },
        { bits: '10', name: 'Internal over-temperature 2', description: 'Shutdown due to internal temperature sensor 2.' },
        { bits: '9', name: 'Internal over-temperature 1', description: 'Shutdown due to internal temperature sensor 1.' },
        { bits: '8', name: 'Inlet over-temperature', description: 'Shutdown due to inlet temperature sensor.' },
        { bits: '7', name: 'Fan2 failure', description: 'Fan2 latched failure; not required and reads zero if fan 2 is absent.' },
        { bits: '6', name: 'Fan1 failure', description: 'Fan1 latched failure.' },
        { bits: '5', name: 'Reserved', description: 'Read as zero.' },
        { bits: '4', name: 'Input loss', description: 'Input AC or DC is not present.' },
        { bits: '3', name: 'Over-temperature', description: 'Any temperature sensor caused an over-temperature shutdown.' },
        { bits: '2', name: 'Over-current', description: 'Hardware current limit or software OCP limit latched the output off.' },
        { bits: '1', name: 'Over-voltage', description: 'Output latched off due to over-voltage.' },
        { bits: '0', name: 'General failure', description: 'Input OK and PSON are true, but Output DCOK is false.' },
      ],
      notes: [
        'Most latched bits clear when PSON transitions from disabled to enabled, input becomes not OK, or standby output drops out of regulation.',
        'Temperature-specific shutdown bits clear after the over-temperature condition clears.',
      ],
    },
  },
  {
    value: 'warning',
    label: 'Warning event',
    address: 0x06,
    access: 'RO',
    description: 'Non-latching warning bits for input/output voltage, output current, and temperature thresholds.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.5.1',
      summary: 'Warning word for threshold crossings that have not necessarily shut the PSU down.',
      format: '16-bit read-only bit field.',
      bitFields: [
        { bits: '15-9', name: 'Reserved', description: 'Read as zero.' },
        { bits: '8', name: 'Output current high', description: 'Output current is higher than 100% of max spec; clears below 95% or when Output DCOK is false.' },
        { bits: '7', name: 'Vaux low', description: 'Aux voltage below the aux min-warning register; not required and reads zero if Vaux sensing is absent.' },
        { bits: '6', name: 'Vaux high', description: 'Aux voltage above the aux max-warning register; not required and reads zero if Vaux sensing is absent.' },
        { bits: '5', name: 'Internal temperature high', description: 'Internal temperature is above the internal temperature warning register; clears 5 deg C below the threshold.' },
        { bits: '4', name: 'Inlet temperature high', description: 'Inlet temperature is above the inlet temperature warning register; clears 5 deg C below the threshold.' },
        { bits: '3', name: 'Vout low', description: 'Main output voltage is below the output-voltage min-warning register; valid only when Output DCOK is true.' },
        { bits: '2', name: 'Vout high', description: 'Main output voltage is above the output-voltage max-warning register; valid only when Output DCOK is true.' },
        { bits: '1', name: 'Vin low', description: 'Input voltage is below the input-voltage min-warning register; valid only when Input OK is true.' },
        { bits: '0', name: 'Vin high', description: 'Input voltage is above the input-voltage max-warning register; valid only when Input OK is true.' },
      ],
      notes: ['The firmware applies hysteresis to prevent warning bits from toggling near thresholds.'],
    },
  },
  {
    value: 'vin',
    label: 'Input voltage',
    address: 0x08,
    access: 'RO',
    description: 'Measured PSU input voltage, encoded as raw / 32 Vrms.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.6.1',
      summary: 'Measured AC or DC input voltage to the power supply.',
      format: "Two's-complement 16-bit value, 1 LSB = 1/32 Vrms.",
      readBehavior: ['Updated at least every 250 ms.', 'Responds to a step change within 1 second.'],
      notes: ['Minimum sensor resolution is 1.4 V; reporting accuracy is +/-5% or better.'],
    },
  },
  {
    value: 'iin',
    label: 'Input current',
    address: 0x0a,
    access: 'RO',
    description: 'Measured PSU input current, encoded as raw / 64 A.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.6.2',
      summary: 'Measured current drawn from the PSU input.',
      format: "Two's-complement 16-bit value, 1 LSB = 1/64 A.",
      readBehavior: ['Updated at least every 250 ms.', 'Responds to a step change within 1 second.'],
      notes: [
        'Minimum reported current is 0.25 A; lower currents are out of range and reported as 0.25 A.',
        'Accuracy: +/-0.05 A for 0.25-1 A, +/-5% for 1-3 A, +/-3% above 3 A.',
      ],
    },
  },
  {
    value: 'pin',
    label: 'Input power',
    address: 0x0c,
    access: 'RO',
    description: 'Measured PSU input power in raw watts.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.6.3',
      summary: 'Measured total RMS input power.',
      format: 'Unsigned-looking 16-bit watt value; 1 LSB = 1 W.',
      readBehavior: ['Updated at least every 250 ms.', 'Responds to a step change within 1 second.'],
      notes: [
        'Capable of reporting down to about 20 W input power; below 20 W is out of range and reported as a constant value.',
        'Accuracy: +/-5 W from 20-100 W and +/-5% above 100 W when calibrated as specified.',
        'For output power above 36 W, reported input power should be greater than reported output power.',
      ],
    },
  },
  {
    value: 'vout',
    label: 'Output voltage',
    address: 0x0e,
    access: 'RO',
    description: 'Measured main 12 V output voltage, encoded as raw / 256 V.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.7.1',
      summary: 'Measured voltage of the main 12 V output.',
      format: "Two's-complement 16-bit value, 1 LSB = 1/256 V.",
      notes: ['Minimum sensor resolution is 60 mV; accuracy is +/-2% or better.'],
    },
  },
  {
    value: 'iout',
    label: 'Output current',
    address: 0x10,
    access: 'RO',
    description: 'Measured main 12 V output current, encoded as raw / 64 A.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.7.2',
      summary: 'Measured current delivered by the main 12 V output.',
      format: "Two's-complement 16-bit value, 1 LSB = 1/64 A.",
      notes: [
        'Minimum output current reported is 3 A; <=3 A is undefined.',
        'Minimum sensor resolution is 500 mA.',
        'Accuracy: +/-50% for 3-6 A, +/-15% for 6-10 A, +/-10% for 10-30 A, +/-5% at 30 A and above.',
      ],
    },
  },
  {
    value: 'pout',
    label: 'Output power',
    address: 0x12,
    access: 'RO',
    description: 'Measured main 12 V output power in raw watts.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.7.3',
      summary: 'Measured total power delivered by the main 12 V output.',
      format: '16-bit watt value; 1 LSB = 1 W.',
      notes: [
        'Capable of reporting output power down to 36 W, equivalent to 3 A on the 12 V output.',
        'Reported output power should not exceed reported input power when output power is greater than 36 W.',
      ],
    },
  },
  {
    value: 'tinlet',
    label: 'Inlet temperature',
    address: 0x1a,
    access: 'RO',
    description: 'Measured inlet-air temperature, encoded as raw / 64 deg C.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.8.1',
      summary: 'Temperature of air entering the power supply.',
      format: "Two's-complement 16-bit value, 1 LSB = 1/64 deg C.",
      notes: [
        'Minimum sensor resolution is 1.5 deg C; accuracy is +/-5 deg C.',
        'Accuracy applies above 25 deg C nominal inlet temperature after the supply reaches thermal equilibrium.',
      ],
    },
  },
  {
    value: 'tinternal',
    label: 'Internal temperature',
    address: 0x1c,
    access: 'RO',
    description: 'Maximum internal hot-spot temperature, encoded as raw / 64 deg C.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.8.2',
      summary: 'Maximum of the internal hot-spot temperature sensors reported by the PSU.',
      format: "Two's-complement 16-bit value, 1 LSB = 1/64 deg C.",
      notes: [
        'The hot-spot sensor location is vendor-defined.',
        'Minimum sensor resolution is 1.5 deg C; accuracy is +/-5 deg C above 25 deg C after thermal equilibrium.',
      ],
    },
  },
  {
    value: 'fan1',
    label: 'Fan 1 speed',
    address: 0x1e,
    access: 'RO',
    description: 'Measured fan 1 tachometer speed in RPM.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.9.1',
      summary: 'Measured speed of PSU fan 1.',
      format: '16-bit RPM value; 1 LSB = 1 RPM.',
      notes: ['Accuracy is +/-5% or better; minimum resolution is 200 RPM.'],
    },
  },
  {
    value: 'avg-input-power-count',
    label: 'Avg input power count',
    address: 0x30,
    access: 'RW',
    description: 'Count of 0.5 s average-input-power samples; write 0x0000 to clear the count and accumulator.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet sections 12.4.6.4 and 12.4.6.7',
      summary: 'Tracks how many half-second average-input-power samples have been accumulated.',
      format: '16-bit count; each count represents one 0.5 s average-power sample.',
      readBehavior: ['Reading this register snapshots the 32-bit average input power accumulator into registers 0x2C and 0x2E.'],
      writeBehavior: ['Writing 0x0000 clears the count and the 32-bit accumulator.', 'The PSU also clears it when Input OK is false.'],
      notes: ['When the count reaches 0xFFFF, accumulation stops until this register is cleared.'],
    },
  },
  {
    value: 'max-input-power',
    label: 'Max input power',
    address: 0x32,
    access: 'RW',
    description: 'Maximum 0.5 s average input power since clear, in raw watts; write 0x0000 to reset.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.6.8',
      summary: 'Stores the maximum half-second average input power observed since the register was cleared.',
      format: '16-bit watt value; 1 LSB = 1 W.',
      readBehavior: ['Active after Input OK is asserted.'],
      writeBehavior: ['Writing 0x0000 clears this maximum and starts a new calculation.', 'Writing any nonzero value is ignored.', 'The PSU also clears it when Input OK is false.'],
    },
  },
  {
    value: 'max-input-current',
    label: 'Max input current',
    address: 0x34,
    access: 'RW',
    description: 'Maximum 0.5 s average input current since clear, encoded as raw / 64 A; write 0x0000 to reset.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.6.9',
      summary: 'Stores the maximum half-second average input current observed since the register was cleared.',
      format: "Two's-complement 16-bit value, 1 LSB = 1/64 A.",
      readBehavior: ['Active after Input OK is asserted.'],
      writeBehavior: ['Writing 0x0000 clears this maximum and starts a new calculation.', 'Writing any nonzero value is ignored.', 'The PSU also clears it when Input OK is false.'],
      notes: ['Minimum resolution and accuracy match the input current register.'],
    },
  },
  {
    value: 'peak-iout',
    label: 'Peak output current',
    address: 0x36,
    access: 'RW',
    description: 'Peak main 12 V output current since clear, encoded as raw / 64 A; write 0x0000 to reset.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.7.4',
      summary: 'Stores the peak current observed on the main 12 V output since the register was cleared.',
      format: "Two's-complement 16-bit value, 1 LSB = 1/64 A.",
      readBehavior: ['Active after PSOK / Output DCOK is asserted.'],
      writeBehavior: ['Writing 0x0000 clears this peak and starts a new calculation.', 'Writing any nonzero value is ignored.', 'The PSU also clears it when Output DCOK is false.'],
      notes: ['Minimum resolution and accuracy match the output current register.'],
    },
  },
  {
    value: 'ps-control',
    label: 'PS control',
    address: 0x3a,
    access: 'RW',
    description: 'Control bit field for interrupts, fan-control ownership, system type, voltage modes, fan-fault behavior, LED behavior, and standby power save.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.3.1',
      summary: 'Limited external-control register for PSU features. Some bits are host-writable requests; some are set or cleared by the PSU firmware.',
      format: '16-bit bit field; default 0x0000.',
      defaultValue: '0x0000',
      bitFields: [
        { bits: '15-14', name: 'Reserved', description: 'Read as zero.' },
        { bits: '13', name: 'SF6 standby power save', description: 'Requests standby power save when PSON is high, no main 12 V is detected, and the feature is supported.' },
        { bits: '12', name: 'SF5 Vout step down', description: 'Requests 12.05 V +/-30 mV and disables droop-share mode if supported.' },
        { bits: '11', name: 'SF4 adjustable Vout', description: 'Uses Output Voltage Set register 0x58 and disables droop-share mode if supported.' },
        { bits: '10', name: 'SF3 Vout step up', description: 'Not required; if supported, requests about 12.40 V +/-40 mV at 1 A load.' },
        { bits: '9', name: 'SF2 fan fault disabled', description: 'Allows operation with failed fan reporting while still setting fan-failure event bits if supported.' },
        { bits: '8', name: 'SF1 LED on in standby', description: 'Keeps PSU LED on in standby when input is OK unless the PSU has failed.' },
        { bits: '7', name: 'Fan2 system control', description: 'Set by PSU after a nonzero Fan2 Control write; clear it to relinquish fan 2. Read as zero if fan 2 is absent.' },
        { bits: '6', name: 'Fan1 system control', description: 'Set by PSU after a nonzero Fan1 Control write; clear it to relinquish fan 1. Writing one directly is ignored.' },
        { bits: '5', name: 'Interrupt', description: 'Set by PSU after asserting PS_INTERRUPT. The host can clear it; writing one is ignored.' },
        { bits: '4', name: 'Warning event mask', description: '1 enables warning events to assert PS_INTERRUPT.' },
        { bits: '3', name: 'Shutdown event mask', description: '1 enables shutdown events to assert PS_INTERRUPT.' },
        { bits: '2-0', name: 'System type', description: '0 ML/DL/default, 1 blade, 2 redundant fan system, 3 Vout step up not required, 4 adjustable Vout, 5 Vout step down; 6-7 reserved and ignored.' },
      ],
      notes: [
        'If multiple voltage-adjustment requests are active, priority is adjustable Vout first, step down second, step up third.',
        'Unsupported feature requests are ignored by the PSU.',
      ],
    },
  },
  {
    value: 'fan1-control',
    label: 'Fan 1 control',
    address: 0x3c,
    access: 'RW',
    description: 'Request a fan 1 RPM floor; nonzero requests external fan control, while 0x0000 writes are ignored.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.9.3',
      summary: 'Desired fan 1 speed in RPM when the external system wants to control the fan.',
      format: '16-bit RPM value.',
      defaultValue: '0x0000',
      writeBehavior: [
        'Writing a nonzero RPM sets PS Control bit 6, meaning the external system controls or wants to control fan 1.',
        'Writing 0x0000 is ignored.',
        'To relinquish fan 1, clear PS Control bit 6; the PSU then clears this register and resumes internal fan control.',
      ],
      notes: ['The PSU may override upward for thermal needs but must not command fan 1 below the requested speed. If overridden, PS Status bit 6 is set.'],
    },
  },
  {
    value: 'fan2-control',
    label: 'Fan 2 control',
    address: 0x3e,
    access: 'RW',
    description: 'Optional fan 2 RPM request; ignored and reads 0x0000 when fan 2 is absent.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.9.4',
      summary: 'Desired fan 2 speed in RPM for PSUs that have a second fan.',
      format: '16-bit RPM value.',
      defaultValue: '0x0000',
      writeBehavior: [
        'Writing a nonzero RPM sets PS Control bit 7 on supplies with fan 2.',
        'Writing 0x0000 is ignored.',
        'To relinquish fan 2, clear PS Control bit 7; the PSU then clears this register and resumes internal fan control.',
        'If fan 2 is not available, writes are ignored and reads always return 0x0000.',
      ],
      notes: ['The PSU may override upward for thermal needs but must not command fan 2 below the requested speed. If overridden, PS Status bit 7 is set.'],
    },
  },
  {
    value: 'vout-set',
    label: 'Output voltage set',
    address: 0x58,
    access: 'RW',
    description: 'Adjustable 12 V output setpoint, encoded as raw / 1024 V; effective only in adjustable-Vout mode and clamped to 12.0-12.3 V.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.3.2',
      summary: 'Target voltage for the main 12 V output when adjustable-Vout mode is selected.',
      format: '16-bit value, 1 LSB = 1/1024 V.',
      defaultValue: '0x3133, about 12.30 V.',
      legalValues: [
        'Effective target range is 12.0 V through 12.3 V, about raw 0x3000 through 0x3133.',
        'Writes above 12.3 V are limited by PSU firmware to 12.3 V; writes below 12.0 V are limited to 12.0 V.',
        'This register only affects output when PS Control system type bits are 100b or SF4 adjustable Vout is enabled.',
      ],
      notes: [
        'Adjustable Vout disables droop-share mode.',
        'The datasheet specifies +/-1% accuracy, with sensor resolution of 20 mV at 10% load and 3 mV above 10% load.',
      ],
    },
  },
  {
    value: 'ocp1',
    label: 'Over-current limit 1',
    address: 0x5a,
    access: 'RW',
    description: 'Software over-current-protection threshold for the main 12 V output, encoded as raw / 64 A; 10-40 ms timer.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.3.3',
      summary: 'Software over-current-protection (OCP) set point 1 for the main 12 V output. This is the short-timer limit.',
      format: "Two's-complement 16-bit value, 1 LSB = 1/64 A.",
      defaultValue: '0x2300 in the register table.',
      legalValues: [
        'The software threshold is effective only when written below the non-adjustable hardware OCP threshold.',
        'Writing a value above the hardware OCP threshold cannot raise the PSU hardware current limit.',
        'Setting this too low can latch the 12 V output off if output current exceeds it for the timer interval.',
      ],
      writeBehavior: ['If PSON has turned on, PSOK is low, output current exceeds this register, and the 10-40 ms OCP1 timer expires, the output latches off and Shutdown Event bit 11 is set.'],
    },
  },
  {
    value: 'ocp2',
    label: 'Over-current limit 2',
    address: 0x5c,
    access: 'RW',
    description: 'Software over-current-protection threshold for the main 12 V output, encoded as raw / 64 A; 100-200 ms timer.',
    help: {
      source: 'Compatible-family PS-2751-7H-LF datasheet section 12.4.3.4',
      summary: 'Software over-current-protection (OCP) set point 2 for the main 12 V output. This is the longer sustained-current limit.',
      format: "Two's-complement 16-bit value, 1 LSB = 1/64 A.",
      defaultValue: '0x2300 in the register table.',
      legalValues: [
        'The software threshold is effective only when written below the non-adjustable hardware OCP threshold.',
        'Writing a value above the hardware OCP threshold cannot raise the PSU hardware current limit.',
        'Setting this too low can latch the 12 V output off if output current exceeds it for the timer interval.',
      ],
      writeBehavior: ['If PSON has turned on, PSOK is low, output current exceeds this register, and the 100-200 ms OCP2 timer expires, the output latches off and Shutdown Event bit 12 is set.'],
    },
  },
]

export const DEFAULT_MONITOR_REGISTERS = [
  0x02,
  0x04,
  0x06,
  0x08,
  0x0a,
  0x0c,
  0x0e,
  0x10,
  0x12,
  0x1a,
  0x1c,
  0x1e,
  0x30,
  0x32,
  0x34,
  0x36,
  0x3a,
] as const

const REGISTER_ALIASES = new Map<string, number>([
  ['ucinfo', 0x00],
  ['psstatus', 0x02],
  ['status', 0x02],
  ['shutdownevent', 0x04],
  ['shutdown', 0x04],
  ['warningevent', 0x06],
  ['warning', 0x06],
  ['inputvoltage', 0x08],
  ['vin', 0x08],
  ['inputcurrent', 0x0a],
  ['iin', 0x0a],
  ['inputpower', 0x0c],
  ['pin', 0x0c],
  ['outputvoltage', 0x0e],
  ['vout', 0x0e],
  ['outputcurrent', 0x10],
  ['iout', 0x10],
  ['outputpower', 0x12],
  ['pout', 0x12],
  ['inlettemperature', 0x1a],
  ['tinlet', 0x1a],
  ['internaltemperature', 0x1c],
  ['tinternal', 0x1c],
  ['fan1speed', 0x1e],
  ['fan1', 0x1e],
  ['averageinputpowercount', 0x30],
  ['avginputpowercount', 0x30],
  ['maxhalfsecondinputpower', 0x32],
  ['maxinputpower', 0x32],
  ['maxhalfsecondinputcurrent', 0x34],
  ['maxinputcurrent', 0x34],
  ['peakoutputcurrent', 0x36],
  ['peakiout', 0x36],
  ['pscontrol', 0x3a],
  ['control', 0x3a],
  ['fan1control', 0x3c],
  ['fan2control', 0x3e],
  ['outputvoltageset', 0x58],
  ['voutset', 0x58],
  ['ocp1', 0x5a],
  ['overcurrentsetpoint1', 0x5a],
  ['ocp2', 0x5c],
  ['overcurrentsetpoint2', 0x5c],
])

export function registerValue(input: RegisterSelector): number {
  const value =
    typeof input === 'number'
      ? checkedInteger(input, 0xff)
      : (REGISTER_ALIASES.get(normalize(input)) ?? parseU8(input))
  if ((value & 1) !== 0) {
    throw new PsuBackendError(
      `LiteOn register 0x${hex(value, 2)} is odd; the uC protocol only accepts even registers`,
    )
  }
  return value
}

export function u8Value(input: RegisterValue): number {
  return typeof input === 'number' ? checkedInteger(input, 0xff) : parseU8(input)
}

export function u16Value(input: RegisterValue): number {
  return typeof input === 'number' ? checkedInteger(input, 0xffff) : parseU16(input)
}

export function registerWriteValidationError(register: RegisterOption, raw: number): string {
  if (register.access !== 'RW') {
    return 'Only known RW LiteOn registers can be written from this UI'
  }
  if (!Number.isInteger(raw) || raw < 0 || raw > 0xffff) {
    return `${register.label} must be a 16-bit value`
  }

  switch (register.address) {
    case 0x30:
    case 0x32:
    case 0x34:
    case 0x36:
      return raw === 0
        ? ''
        : `${register.label} only accepts 0 because nonzero writes are ignored by the PSU`
    case 0x3a: {
      if ((raw & 0xc000) !== 0) {
        return 'PS control reserved bits 15-14 must be 0'
      }
      const systemType = raw & 0x0007
      if (systemType > 5) {
        return 'PS control system type bits must be 0..5; 6 and 7 are reserved'
      }
      return ''
    }
    case 0x3c:
    case 0x3e:
      return raw > 0
        ? ''
        : `${register.label} requires a nonzero RPM; clear fan ownership with the PS control action`
    case 0x58:
      return raw >= 0x3000 && raw <= 0x3133
        ? ''
        : 'Output voltage set must be 0x3000..0x3133, equivalent to 12.0 V through 12.3 V'
    case 0x5a:
    case 0x5c:
      return raw > 0
        ? ''
        : `${register.label} must be greater than 0 A`
    default:
      return ''
  }
}

export function checkedRegisterWrite(
  register: RegisterSelector,
  value: RegisterValue,
): { register: number; value: number } {
  const registerAddress = registerValue(register)
  const registerOption = REGISTER_OPTIONS.find((option) => option.address === registerAddress)
  if (!registerOption) {
    throw new PsuBackendError(
      `LiteOn register 0x${hex(registerAddress, 2)} is not in the supported register map`,
    )
  }

  const raw = u16Value(value)
  const validationError = registerWriteValidationError(registerOption, raw)
  if (validationError) {
    throw new PsuBackendError(validationError)
  }

  return { register: registerAddress, value: raw }
}

export function registerKeyForAddress(address: number): string | null {
  return REGISTER_OPTIONS.find((option) => option.address === address)?.value ?? null
}

export function statusFromRegisterValues(
  values: Record<string, string>,
  address: string,
  pins: PsuPins | null,
  rawLines: string[] = [],
  slot: number | null = null,
): PsuStatus | null {
  const ucInfo = registerHex(values, 0x00)
  const psStatus = registerHex(values, 0x02)
  const psControl = registerHex(values, 0x3a)
  if (ucInfo == null || psStatus == null || psControl == null) {
    return null
  }

  const ucInfoWord = wordFromHex(ucInfo)
  const psStatusWord = wordFromHex(psStatus)
  const psControlWord = wordFromHex(psControl)
  const shutdownEvent = registerHex(values, 0x04) ?? '0x0000'
  const warningEvent = registerHex(values, 0x06) ?? '0x0000'
  const raw = {
    ADDRESS: address,
    UCINFO: ucInfo,
    PSSTATUS: psStatus,
    SHUTDOWN: shutdownEvent,
    WARNING: warningEvent,
    PSCONTROL: psControl,
  }

  return {
    slot,
    address,
    ucInfo,
    psStatus,
    firmware: `${(ucInfoWord >> 12) & 0x0f}.${(ucInfoWord >> 8) & 0x0f}`,
    ucType: String((ucInfoWord >> 4) & 0x0f),
    vendor: String((psStatusWord >> 3) & 0x07),
    inputOk: (psStatusWord & (1 << 1)) !== 0,
    outputOk: (psStatusWord & (1 << 0)) !== 0,
    psuPsonAsserted: (psStatusWord & (1 << 2)) !== 0,
    range: inputRangeName((psStatusWord >> 8) & 0x07),
    shutdownEvent,
    warningEvent,
    psControl,
    interruptLatched: (psControlWord & (1 << 5)) !== 0,
    pins,
    telemetry: telemetryFromRegisterValues(values),
    raw,
    rawLines,
  }
}

export function efficiencyPercent(inputPowerW: number, outputPowerW: number): number | null {
  if (inputPowerW >= 20 && outputPowerW >= 36 && outputPowerW < inputPowerW) {
    return (outputPowerW / inputPowerW) * 100
  }
  return null
}

function registerHex(values: Record<string, string>, address: number): string | null {
  const key = registerKeyForAddress(address)
  const value = key == null ? undefined : values[key]
  if (value == null || value === '-' || value === '?') {
    return null
  }
  return value
}

function telemetryFromRegisterValues(values: Record<string, string>): PsuTelemetry | null {
  const inputVoltageRaw = registerHex(values, 0x08)
  const inputCurrentRaw = registerHex(values, 0x0a)
  const inputPowerRaw = registerHex(values, 0x0c)
  const outputVoltageRaw = registerHex(values, 0x0e)
  const outputCurrentRaw = registerHex(values, 0x10)
  const outputPowerRaw = registerHex(values, 0x12)
  const inletTemperatureRaw = registerHex(values, 0x1a)
  const internalTemperatureRaw = registerHex(values, 0x1c)
  const fan1SpeedRaw = registerHex(values, 0x1e)

  if (
    inputVoltageRaw == null ||
    inputCurrentRaw == null ||
    inputPowerRaw == null ||
    outputVoltageRaw == null ||
    outputCurrentRaw == null ||
    outputPowerRaw == null ||
    inletTemperatureRaw == null ||
    internalTemperatureRaw == null ||
    fan1SpeedRaw == null
  ) {
    return null
  }

  const inputVoltageVrms = scaledSigned(inputVoltageRaw, 32)
  const inputCurrentA = scaledSigned(inputCurrentRaw, 64)
  const inputPowerW = wordFromHex(inputPowerRaw)
  const outputVoltageV = scaledSigned(outputVoltageRaw, 256)
  const outputCurrentA = scaledSigned(outputCurrentRaw, 64)
  const outputPowerW = wordFromHex(outputPowerRaw)
  const inletTemperatureC = scaledSigned(inletTemperatureRaw, 64)
  const internalTemperatureC = scaledSigned(internalTemperatureRaw, 64)
  const fan1SpeedRpm = wordFromHex(fan1SpeedRaw)

  return {
    inputVoltageRaw,
    inputVoltageVrms,
    inputCurrentRaw,
    inputCurrentA,
    inputPowerRaw,
    inputPowerW,
    outputVoltageRaw,
    outputVoltageV,
    outputCurrentRaw,
    outputCurrentA,
    outputPowerRaw,
    outputPowerW,
    efficiencyPercent: efficiencyPercent(inputPowerW, outputPowerW),
    inletTemperatureRaw,
    inletTemperatureC,
    internalTemperatureRaw,
    internalTemperatureC,
    fan1SpeedRaw,
    fan1SpeedRpm,
    raw: {
      VIN_RAW: inputVoltageRaw,
      IIN_RAW: inputCurrentRaw,
      PIN_RAW: inputPowerRaw,
      VOUT_RAW: outputVoltageRaw,
      IOUT_RAW: outputCurrentRaw,
      POUT_RAW: outputPowerRaw,
      TINLET_RAW: inletTemperatureRaw,
      TINTERNAL_RAW: internalTemperatureRaw,
      FAN1_RAW: fan1SpeedRaw,
    },
  }
}

function inputRangeName(range: number): string {
  switch (range) {
    case 0:
      return 'out-of-range'
    case 1:
      return 'low-line'
    case 2:
      return 'mid-line'
    case 3:
      return 'high-line'
    case 4:
      return '48VDC'
    case 5:
      return '420VDC'
    default:
      return 'reserved'
  }
}

function scaledSigned(raw: string, divisor: number): number {
  const word = wordFromHex(raw)
  const signed = word & 0x8000 ? word - 0x10000 : word
  return signed / divisor
}

function wordFromHex(raw: string): number {
  return parseU16(raw)
}

function parseU8(input: string): number {
  return checkedInteger(parseU16(input), 0xff)
}

function parseU16(input: string): number {
  const trimmed = input.trim()
  const radix = trimmed.toLowerCase().startsWith('0x') ? 16 : 10
  const digits = radix === 16 ? trimmed.slice(2) : trimmed
  const validDigits = radix === 16 ? /^[0-9a-f]+$/i : /^[0-9]+$/
  if (digits.length === 0 || !validDigits.test(digits)) {
    throw new PsuBackendError(`could not parse number ${input}`)
  }
  return checkedInteger(Number.parseInt(digits, radix), 0xffff)
}

function checkedInteger(value: number, max: number): number {
  if (!Number.isInteger(value) || value < 0 || value > max) {
    throw new PsuBackendError(`value ${value} is outside 0..0x${hex(max, 2)}`)
  }
  return value
}

function normalize(input: string): string {
  return input
    .split('')
    .filter((char) => char !== '-' && char !== '_' && !/\s/.test(char))
    .join('')
    .toLowerCase()
}

function hex(value: number, width: number): string {
  return value.toString(16).toUpperCase().padStart(width, '0')
}
