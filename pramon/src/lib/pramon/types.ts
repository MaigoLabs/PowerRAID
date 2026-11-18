import type { RegisterOption } from '$lib/psu'

export type Tone = 'default' | 'good' | 'warn' | 'bad' | 'muted'

export type MetricRow = {
  label: string
  value: string
  detail: string
  tone: Tone
}

export type CompactRowData = {
  label: string
  value: string
  tone: Tone
}

export type TelemetryPoint = {
  timestamp: number
  value: number
}

export type ChartPoint = TelemetryPoint & {
  x: number
  y: number
}

export type TelemetrySample = {
  timestamp: number
  inputVoltageVrms: number
  inputPowerW: number
  outputVoltageV: number
  outputCurrentA: number
  outputPowerW: number
  efficiencyPercent: number | null
  inletTemperatureC: number
  internalTemperatureC: number
  fan1SpeedRpm: number
}

export type TelemetrySeriesKey = Exclude<keyof TelemetrySample, 'timestamp'>

export type TelemetryMetricRow = {
  key: TelemetrySeriesKey
  label: string
  value: string
  tone: Tone
}

export type TelemetrySeriesPath = {
  slot: number
  label: string
  color: string
  path: string
  area: string
  latest: string
  sampleCount: number
}

export type ChartTick = {
  value: number
  y: number
  label: string
}

export type RegisterTableEntry = {
  value: string
  result: string
  updatedAt: number
  cycle: number | null
}

export type RegisterTableRow = {
  register: RegisterOption
  listened: boolean
  liveValue: string
  parsedValue: string
  updateText: string
  updateTone: Tone
}

export type PsControlActionRow = {
  label: string
  bit: number
  clearOnly?: boolean
  caution?: boolean
  active: boolean
  disabled: boolean
  className: string
}

export type PsControlSystemTypeRow = {
  label: string
  value: number
  caution: boolean
  active: boolean
  disabled: boolean
  className: string
}
