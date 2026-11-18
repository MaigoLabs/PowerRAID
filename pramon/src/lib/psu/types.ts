export type PsonCommand = 'on' | 'off' | 'status'
export type PsuCadenceMode = 'normal' | 'fast'
export type ResetTarget = 'peak' | 'average'
export type RegisterSelector = string | number
export type RegisterValue = string | number
export type PsuSlotId = 1 | 2 | 3
export type PsuMonitorSlot = PsuSlotId | 'all'
export type PsuSlotSelector = number | 'all'

export interface PsuCommandResponse {
  command: string
  lines: string[]
  ok: boolean
  terminalLine: string
}

export interface PsuPins {
  slot: number | null
  presentN: boolean
  present: boolean
  psokHigh: boolean
  psInterruptN: boolean
  interruptAsserted: boolean
  psonAsserted: boolean
  psonNPinHigh: boolean
  raw: Record<string, string>
  rawLines?: string[]
}

export interface PsuSlotSummary extends PsuPins {
  slot: number
  name: string
  address: string
  detected: boolean
  rawLine: string
}

export interface PsuTelemetry {
  inputVoltageRaw: string
  inputVoltageVrms: number
  inputCurrentRaw: string
  inputCurrentA: number
  inputPowerRaw: string
  inputPowerW: number
  outputVoltageRaw: string
  outputVoltageV: number
  outputCurrentRaw: string
  outputCurrentA: number
  outputPowerRaw: string
  outputPowerW: number
  efficiencyPercent: number | null
  inletTemperatureRaw: string
  inletTemperatureC: number
  internalTemperatureRaw: string
  internalTemperatureC: number
  fan1SpeedRaw: string
  fan1SpeedRpm: number
  raw: Record<string, string>
}

export interface PsuStatus {
  slot: number | null
  address: string
  ucInfo: string
  psStatus: string
  firmware: string
  ucType: string
  vendor: string
  inputOk: boolean
  outputOk: boolean
  psuPsonAsserted: boolean
  range: string
  shutdownEvent: string
  warningEvent: string
  psControl: string
  interruptLatched: boolean
  pins: PsuPins | null
  telemetry: PsuTelemetry | null
  raw: Record<string, string>
  rawLines: string[]
}

export interface PsuAuthStatus {
  configured: boolean
  identity: string
  salt: string
  iterations: number
  telemetryPublic: boolean
  lockoutMs: number
  sessionTtlMs: number
  passwordMinLength: number
  passwordMaxLength: number
}

export interface PsuFirmwareInfo {
  firmware: string
  protocol: number
  build: string
  cadence: PsuCadenceMode | string
  address: string
  detected: boolean
  wifiStatus: string
  ip: string
  apiEnabled: boolean
  auth: PsuAuthStatus | null
  influx: PsuInfluxStatus | null
  raw: Record<string, string>
  rawLines?: string[]
}

export interface PsuWifiStatus {
  status: string
  connected: boolean
  remembered: boolean
  hostname: string
  mdnsStarted: boolean
  ssid: string | null
  ip: string | null
  rssi: number | null
  apiEnabled: boolean
  raw: Record<string, string>
  rawLines?: string[]
}

export interface PsuInfluxStatus {
  enabled: boolean
  configured: boolean
  url: string
  org: string
  bucket: string
  measurement: string
  intervalMs: number
  tokenSet: boolean
  lastResult: string
  lastError: string
  lastHttpStatus: number
  lastAttemptMs: number
  lastSuccessMs: number
  writeCount: number
  errorCount: number
  raw: Record<string, string>
  rawLines?: string[]
}

export interface PsuInfluxConfig {
  enabled: boolean
  url: string
  org: string
  bucket: string
  measurement: string
  intervalMs: number
  token?: string
  clearToken?: boolean
}

export interface PsuMonitorState {
  slot: number | 'all' | null
  active: boolean
  intervalMs: number
  count: number
  cadence: PsuCadenceMode | string
  raw: Record<string, string>
  rawLines?: string[]
}

export interface PsuMonitorCycle {
  slot: number | 'all' | null
  cycle: number
  phase: 'start' | 'end' | 'unknown'
  ms: number | null
  raw: Record<string, string>
}

export interface PsuRegisterUpdate {
  slot: number | null
  cycle: number | null
  address: string
  register: string
  registerAddress: number
  value: string | null
  result: string
  ms: number | null
  ok: boolean
  raw: Record<string, string>
  rawLine: string
}

export interface PsuProbeAttempt {
  slot: number | null
  add: number
  addBits: string
  address: string
  result: string
  ucInfo: string | null
  present: boolean | null
  raw: Record<string, string>
}

export interface PsuProbeResult {
  slot: number | 'all' | null
  ok: boolean
  result: string
  address: string | null
  ucInfo: string | null
  attempts: PsuProbeAttempt[]
  rawLines: string[]
}

export interface PsuAddress {
  slot: number | null
  address: string
  detected: boolean | null
  raw: Record<string, string>
  rawLines?: string[]
}

export interface PsuRegisterRead {
  slot: number | null
  address: string
  register: string
  value: string
  raw: Record<string, string>
  rawLines?: string[]
}

export interface PsuRegisterWrite extends PsuRegisterRead {}

export interface PsuPsonState {
  slot: number | 'all' | null
  asserted: boolean
  psonNPinHigh: boolean
  automatic: boolean
  powerPolicy: PsuPowerPolicy | null
  raw: Record<string, string>
  rawLines?: string[]
}

export interface PsuPowerPolicySlot {
  slot: number
  present: boolean
  psonAsserted: boolean
  psokHigh: boolean
  statusResult: string
  outputPowerResult: string
  psStatus: string
  inputOk: boolean
  outputOk: boolean
  psuPsonAsserted: boolean
  outputPowerW: number
  candidate: boolean
  failed: boolean
  startupGrace: boolean
  raw: Record<string, string>
}

export interface PsuPowerPolicy {
  automatic: boolean
  nRedundancy: number
  efficiencyProfile: string
  efficiencyProfileLabel: string
  availableEfficiencyProfiles: PsuEfficiencyProfileOption[]
  psuWatts: number
  presentCount: number
  candidateCount: number
  activeCount: number
  targetActiveCount: number
  nonRedundantCount: number
  maxOutputW: number
  totalOutputW: number
  telemetryValid: boolean
  limitExceeded: boolean
  redundancyShortfall: boolean
  lastServiceMs: number
  lastDecisionMs: number
  slots: PsuPowerPolicySlot[]
  raw: Record<string, string>
  rawLines?: string[]
}

export interface PsuPowerPolicyUpdate {
  automatic?: boolean
  nRedundancy?: number
  efficiencyProfile?: string
}

export interface PsuEfficiencyProfileOption {
  id: string
  label: string
}

export interface PsuBackend {
  readonly connected: boolean
  /** True once this browser holds a session for a password-protected bridge. */
  readonly authenticated: boolean
  /**
   * True when the bridge answers reads: open, unlocked, or publishing telemetry
   * for everyone.
   */
  readonly canRead: boolean
  /**
   * True when the bridge can be written to: either no password is set, or a
   * session was established.
   */
  readonly canWrite: boolean

  connect(): Promise<void>
  disconnect(): Promise<void>
  info(): Promise<PsuFirmwareInfo>
  authStatus(): Promise<PsuAuthStatus>
  /** Establishes a session from the password. Resolves to the new status. */
  login(password: string): Promise<PsuAuthStatus>
  logout(): Promise<PsuAuthStatus>
  /** Sets or replaces the password. The caller must be able to write. */
  setPassword(password: string): Promise<PsuAuthStatus>
  /** Removes the password, making the bridge open again. */
  clearPassword(): Promise<PsuAuthStatus>
  setTelemetryPublic(enabled: boolean): Promise<PsuAuthStatus>
  slots(): Promise<PsuSlotSummary[]>
  probe(slot?: number): Promise<PsuProbeResult>
  pins(slot?: number): Promise<PsuPins>
  startMonitor(intervalMs: number, registers?: RegisterSelector[], slot?: PsuMonitorSlot): Promise<PsuMonitorState>
  stopMonitor(): Promise<PsuMonitorState>
  setCadence(mode: PsuCadenceMode): Promise<PsuCommandResponse>
  address(address?: RegisterValue): Promise<PsuAddress>
  readRegister(register: RegisterSelector, slot?: number): Promise<PsuRegisterRead>
  writeRegister(register: RegisterSelector, value: RegisterValue, slot?: number): Promise<PsuRegisterWrite>
  pson(command: PsonCommand, slot?: PsuSlotSelector): Promise<PsuPsonState>
  powerPolicy(update?: PsuPowerPolicyUpdate): Promise<PsuPowerPolicy>
  wifiStatus(): Promise<PsuWifiStatus>
  wifiConnect(ssid: string, password?: string): Promise<PsuWifiStatus>
  wifiReconnect(): Promise<PsuWifiStatus>
  wifiDisconnect(): Promise<PsuWifiStatus>
  wifiForget(): Promise<PsuWifiStatus>
  influxStatus(): Promise<PsuInfluxStatus>
  configureInflux(config: PsuInfluxConfig): Promise<PsuInfluxStatus>
  clearInterrupt(slot?: number): Promise<PsuCommandResponse>
  reset(target: ResetTarget, slot?: number): Promise<PsuCommandResponse>
}

export class PsuBackendError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'PsuBackendError'
  }
}
