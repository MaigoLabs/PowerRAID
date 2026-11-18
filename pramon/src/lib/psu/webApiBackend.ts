import {
  checkedRegisterWrite,
  registerValue,
  u8Value,
} from './protocol'
import { beginSrpLogin, deriveVerifier, hexToBytes } from './srp'
import {
  type PsonCommand,
  type PsuAddress,
  type PsuBackend,
  PsuBackendError,
  type PsuCadenceMode,
  type PsuCommandResponse,
  type PsuAuthStatus,
  type PsuFirmwareInfo,
  type PsuInfluxConfig,
  type PsuInfluxStatus,
  type PsuMonitorCycle,
  type PsuMonitorSlot,
  type PsuMonitorState,
  type PsuPins,
  type PsuPowerPolicy,
  type PsuPowerPolicySlot,
  type PsuPowerPolicyUpdate,
  type PsuProbeAttempt,
  type PsuProbeResult,
  type PsuPsonState,
  type PsuSlotSelector,
  type PsuRegisterRead,
  type PsuRegisterUpdate,
  type PsuRegisterWrite,
  type PsuSlotSummary,
  type PsuWifiStatus,
  type RegisterSelector,
  type RegisterValue,
  type ResetTarget,
} from './types'

const DEFAULT_TIMEOUT_MS = 8_000
const WIFI_TIMEOUT_MS = 20_000
const LOGIN_TIMEOUT_MS = 30_000
const MONITOR_MAX_REGISTERS = 24
const AUTH_HEADER = 'X-PowerRAID-Auth'
const SESSION_STORE_KEY = 'pramon.sessions'

export interface WebApiPsuBackendOptions {
  baseUrl?: string
  timeoutMs?: number
  onPins?: (pins: PsuPins) => void
  onRegisterUpdate?: (update: PsuRegisterUpdate) => void
  onMonitorCycle?: (cycle: PsuMonitorCycle) => void
  onMonitorStreamError?: (error: Error) => void
}

type ApiQueryValue = string | number | boolean | null | undefined

type ApiRequestOptions = {
  method?: 'GET' | 'POST'
  query?: Record<string, ApiQueryValue>
  timeoutMs?: number
  acceptError?: boolean
  /** Sends no session token, for the login exchange itself. */
  anonymous?: boolean
}

type ApiPins = {
  slot?: number
  name?: string
  address?: string
  detected?: boolean
  presentN?: number | boolean
  present?: boolean
  psok?: number | boolean
  psInterruptN?: number | boolean
  interruptAsserted?: boolean
  psonAsserted?: boolean
  psonNPin?: number | boolean
  i2cEnable?: boolean
  i2cEnablePin?: number | boolean
}

type ApiPowerPolicySlot = {
  slot?: number
  present?: boolean
  psonAsserted?: boolean
  psok?: number | boolean
  statusResult?: string
  outputPowerResult?: string
  psStatus?: string
  inputOk?: boolean
  outputOk?: boolean
  psuPsonAsserted?: boolean
  outputPowerW?: number
  candidate?: boolean
  failed?: boolean
  startupGrace?: boolean
}

type ApiMonitorEvent = {
  type?: string
  cycle?: number
  slot?: number | string | null
  address?: string
  register?: string
  result?: string
  value?: string
  ms?: number
  phase?: string
  pins?: ApiPins
  monitor?: Record<string, unknown>
}

export function normalizeWebApiBaseUrl(input: string): string {
  let raw = input.trim()
  if (raw.length === 0) {
    throw new PsuBackendError('Enter the bridge API base URL')
  }
  if (!/^[a-z][a-z0-9+.-]*:\/\//i.test(raw)) {
    raw = `http://${raw}`
  }

  const url = new URL(raw)
  url.hash = ''
  url.search = ''
  let path = url.pathname.replace(/\/+$/, '')
  if (path.endsWith('/api/v1/info')) {
    path = path.slice(0, -'/info'.length)
  }
  if (!path.endsWith('/api/v1')) {
    path = `${path}/api/v1`
  }
  url.pathname = path
  return url.toString().replace(/\/$/, '')
}

export function defaultWebApiBaseUrl(): string {
  if (typeof window === 'undefined') {
    return ''
  }

  const params = new URLSearchParams(window.location.search)
  const fromQuery = params.get('api')
  if (fromQuery) {
    return fromQuery
  }

  const fromStorage = window.localStorage.getItem('pramon.webApiBaseUrl')
  if (fromStorage) {
    return fromStorage
  }

  if (window.location.protocol.startsWith('http') && !['localhost', '127.0.0.1'].includes(window.location.hostname)) {
    return `${window.location.origin}/api/v1`
  }

  return ''
}

export class WebApiPsuBackend implements PsuBackend {
  private baseUrl: string
  private readonly timeoutMs: number
  private readonly onPins?: (pins: PsuPins) => void
  private readonly onRegisterUpdate?: (update: PsuRegisterUpdate) => void
  private readonly onMonitorCycle?: (cycle: PsuMonitorCycle) => void
  private readonly onMonitorStreamError?: (error: Error) => void
  private connectedFlag = false
  private monitorEvents: EventSource | null = null
  private monitorActive = false
  private monitorIntervalMs = 1_000
  private monitorRegisters: number[] = []
  private monitorSlot: PsuMonitorSlot | null = null
  private cadence: PsuCadenceMode | string = 'normal'
  private sessionToken: string | null = null
  private authState: PsuAuthStatus | null = null

  constructor(options: WebApiPsuBackendOptions = {}) {
    this.baseUrl = options.baseUrl ?? ''
    this.timeoutMs = options.timeoutMs ?? DEFAULT_TIMEOUT_MS
    this.onPins = options.onPins
    this.onRegisterUpdate = options.onRegisterUpdate
    this.onMonitorCycle = options.onMonitorCycle
    this.onMonitorStreamError = options.onMonitorStreamError
  }

  get connected(): boolean {
    return this.connectedFlag
  }

  get authenticated(): boolean {
    return this.sessionToken != null
  }

  /**
   * Reads work while locked only when the owner published telemetry. An older
   * firmware without auth reporting is treated as open.
   */
  get canRead(): boolean {
    return this.authState == null || !this.authState.configured ||
      this.authState.telemetryPublic || this.sessionToken != null
  }

  /**
   * Writes need a session only when the bridge has a password. An older
   * firmware without auth reporting is treated as open.
   */
  get canWrite(): boolean {
    return this.authState == null || !this.authState.configured || this.sessionToken != null
  }

  get currentAuth(): PsuAuthStatus | null {
    return this.authState
  }

  async connect(): Promise<void> {
    this.baseUrl = normalizeWebApiBaseUrl(this.baseUrl)
    // A token from an earlier visit may still be valid; send it so a
    // protected bridge reconnects without asking again.
    this.sessionToken = this.loadStoredSession()
    try {
      this.applyAuth(await this.authStatus())
    } catch {
      // Auth reporting is optional on older firmware; info() still works.
      this.authState = null
    }

    // Reaching a locked bridge is a successful connection: the dashboard then
    // offers the unlock form instead of the settings it cannot read yet.
    this.connectedFlag = true
    if (typeof window !== 'undefined') {
      window.localStorage.setItem('pramon.webApiBaseUrl', this.baseUrl)
    }
  }

  async disconnect(): Promise<void> {
    await this.stopMonitor().catch(() => undefined)
    this.connectedFlag = false
  }

  async authStatus(): Promise<PsuAuthStatus> {
    const response = await this.request<Record<string, unknown>>('auth', { anonymous: true })
    return authStatusFromApi(response.auth)
  }

  /**
   * SRP login. The password is hashed locally into a proof; only the proof
   * travels over the network.
   */
  async login(password: string): Promise<PsuAuthStatus> {
    const status = this.authState ?? (await this.authStatus())
    if (!status.configured) {
      throw new PsuBackendError('This bridge has no password set')
    }

    const session = await beginSrpLogin(password, {
      salt: hexToBytes(status.salt),
      iterations: status.iterations,
    })
    const challenge = await this.request<Record<string, unknown>>('auth/challenge', {
      query: { A: session.clientPublic },
      anonymous: true,
      timeoutMs: LOGIN_TIMEOUT_MS,
    })
    const proofs = await session.finish({
      salt: stringField(challenge.salt),
      serverPublic: stringField(challenge.B),
      iterations: numberField(challenge.iterations ?? status.iterations),
    })

    const login = await this.request<Record<string, unknown>>('auth/login', {
      method: 'POST',
      query: { challenge: stringField(challenge.challenge), M1: proofs.clientProof },
      anonymous: true,
      timeoutMs: LOGIN_TIMEOUT_MS,
    })

    // The device proves it knows the verifier too, so a rogue responder cannot
    // harvest a proof and replay it later.
    if (stringField(login.M2).toLowerCase() !== proofs.expectedServerProof) {
      throw new PsuBackendError('bridge failed to prove the password; refusing to trust this session')
    }

    this.sessionToken = stringField(login.token)
    this.storeSession(this.sessionToken)
    this.applyAuth(authStatusFromApi(login.auth))
    return this.authState as PsuAuthStatus
  }

  async logout(): Promise<PsuAuthStatus> {
    if (this.sessionToken == null) {
      return this.authState ?? (await this.authStatus())
    }
    const response = await this.request<Record<string, unknown>>('auth/logout', {
      method: 'POST',
      acceptError: true,
    })
    this.forgetSession()
    this.applyAuth(authStatusFromApi(response.auth))
    return this.authState as PsuAuthStatus
  }

  /** Registers a new password. The device stores only the SRP verifier. */
  async setPassword(password: string): Promise<PsuAuthStatus> {
    const iterations = this.authState?.iterations ?? 600000
    const derived = await deriveVerifier(password, { iterations })
    const response = await this.request<Record<string, unknown>>('auth/password', {
      method: 'POST',
      query: {
        verifier: derived.verifier,
        salt: derived.salt,
        iterations: derived.iterations,
      },
    })
    // Changing the credential re-issues this session and revokes the rest.
    this.sessionToken = stringField(response.token)
    this.storeSession(this.sessionToken)
    this.applyAuth(authStatusFromApi(response.auth))
    return this.authState as PsuAuthStatus
  }

  async clearPassword(): Promise<PsuAuthStatus> {
    const response = await this.request<Record<string, unknown>>('auth/password/clear', {
      method: 'POST',
    })
    this.forgetSession()
    this.applyAuth(authStatusFromApi(response.auth))
    return this.authState as PsuAuthStatus
  }

  async setTelemetryPublic(enabled: boolean): Promise<PsuAuthStatus> {
    const response = await this.request<Record<string, unknown>>('auth/telemetry', {
      method: 'POST',
      query: { enabled },
    })
    this.applyAuth(authStatusFromApi(response.auth))
    return this.authState as PsuAuthStatus
  }

  private applyAuth(status: PsuAuthStatus): void {
    this.authState = status
    // A stored token is useless once the password is gone. Lockout only blocks
    // new logins, so it must not evict a working session.
    if (this.sessionToken != null && !status.configured) {
      this.forgetSession()
    }
  }

  private storeSession(token: string): void {
    if (typeof window === 'undefined') {
      return
    }
    const sessions = readSessionStore()
    sessions[this.baseUrl] = token
    window.localStorage.setItem(SESSION_STORE_KEY, JSON.stringify(sessions))
  }

  private loadStoredSession(): string | null {
    if (typeof window === 'undefined') {
      return null
    }
    return readSessionStore()[this.baseUrl] ?? null
  }

  private forgetSession(): void {
    this.sessionToken = null
    if (typeof window === 'undefined') {
      return
    }
    const sessions = readSessionStore()
    delete sessions[this.baseUrl]
    window.localStorage.setItem(SESSION_STORE_KEY, JSON.stringify(sessions))
  }

  async info(): Promise<PsuFirmwareInfo> {
    return firmwareInfoFromApi(await this.request<Record<string, unknown>>('info'))
  }

  async slots(): Promise<PsuSlotSummary[]> {
    const response = await this.request<{ psus: ApiPins[] }>('psus')
    return response.psus.map(slotSummaryFromApi)
  }

  async probe(slot?: number): Promise<PsuProbeResult> {
    const response = await this.request<Record<string, unknown>>('probe', {
      method: 'POST',
      query: { slot },
      acceptError: true,
    })
    return probeFromApi(response)
  }

  async pins(slot?: number): Promise<PsuPins> {
    const response = await this.request<{ pins: ApiPins | ApiPins[] }>('pins', {
      query: { slot },
    })
    if (Array.isArray(response.pins)) {
      throw new PsuBackendError('pins response returned multiple slots where one was expected')
    }
    return pinsFromApi(response.pins)
  }

  async startMonitor(intervalMs: number, registers: RegisterSelector[] = [], slot?: PsuMonitorSlot): Promise<PsuMonitorState> {
    this.closeMonitorStream()
    this.monitorActive = true
    this.monitorIntervalMs = Math.max(1_000, Math.round(intervalMs))
    this.monitorRegisters = registers.map((register) => registerValue(register)).slice(0, MONITOR_MAX_REGISTERS)
    this.monitorSlot = slot ?? null

    try {
      const response = await this.request<{ monitor: Record<string, unknown> }>('monitor', {
        method: 'POST',
        query: {
          state: 'start',
          slot: this.monitorSlot,
          intervalMs: this.monitorIntervalMs,
          regs: this.monitorRegisters.map(hex8).join(','),
        },
      })
      const state = monitorStateFromApi(objectField(response.monitor), this.monitorState(true))
      this.applyMonitorState(state)
      this.openMonitorStream()
      return state
    } catch (error) {
      this.monitorActive = false
      throw error
    }
  }

  async stopMonitor(): Promise<PsuMonitorState> {
    this.monitorActive = false
    this.closeMonitorStream()
    if (!this.connectedFlag) {
      return this.monitorState(false)
    }

    const response = await this.request<{ monitor: Record<string, unknown> }>('monitor', {
      method: 'POST',
      query: { state: 'stop' },
    })
    const state = monitorStateFromApi(objectField(response.monitor), this.monitorState(false))
    this.applyMonitorState(state)
    return state
  }

  async setCadence(mode: PsuCadenceMode): Promise<PsuCommandResponse> {
    const response = await this.request<Record<string, unknown>>('cadence', {
      method: 'POST',
      query: { mode },
    })
    this.cadence = String(response.mode ?? mode)
    return commandResponse('cadence', response)
  }

  async address(address?: RegisterValue): Promise<PsuAddress> {
    const query: Record<string, ApiQueryValue> = {}
    if (address != null) {
      const address7 = u8Value(address)
      if (address7 < 0x58 || address7 > 0x5f) {
        throw new PsuBackendError('LiteOn monitor address must be in the 0x58..0x5F range')
      }
      query.address = address7
    }
    const response = await this.request<Record<string, unknown>>('address', {
      method: address == null ? 'GET' : 'POST',
      query,
    })
    return addressFromApi(response)
  }

  async readRegister(register: RegisterSelector, slot?: number): Promise<PsuRegisterRead> {
    const reg = registerValue(register)
    const response = await this.request<Record<string, unknown>>('read', {
      query: { slot, reg: hex8(reg) },
    })
    return registerReadFromApi(response)
  }

  async writeRegister(
    register: RegisterSelector,
    value: RegisterValue,
    slot?: number,
  ): Promise<PsuRegisterWrite> {
    const checked = checkedRegisterWrite(register, value)
    const response = await this.request<Record<string, unknown>>('write', {
      method: 'POST',
      query: { slot, reg: hex8(checked.register), value: hex16(checked.value) },
    })
    return registerReadFromApi(response)
  }

  async pson(command: PsonCommand, slot?: PsuSlotSelector): Promise<PsuPsonState> {
    const response = await this.request<{ pson: Record<string, unknown> }>('pson', {
      method: command === 'status' ? 'GET' : 'POST',
      query: { slot, state: command },
    })
    return psonFromApi(response.pson)
  }

  async powerPolicy(update: PsuPowerPolicyUpdate = {}): Promise<PsuPowerPolicy> {
    const hasUpdate = update.automatic != null || update.nRedundancy != null || update.efficiencyProfile != null
    const response = await this.request<{ powerPolicy: Record<string, unknown> }>('power-policy', {
      method: hasUpdate ? 'POST' : 'GET',
      query: {
        automatic: update.automatic == null ? undefined : update.automatic ? 1 : 0,
        n_redundancy: update.nRedundancy,
        efficiency_profile: update.efficiencyProfile,
      },
    })
    return powerPolicyFromApi(response.powerPolicy)
  }

  async wifiStatus(): Promise<PsuWifiStatus> {
    const response = await this.request<{ wifi: Record<string, unknown> }>('wifi')
    return wifiFromApi(response.wifi)
  }

  async wifiConnect(ssid: string, password = ''): Promise<PsuWifiStatus> {
    const response = await this.request<{ wifi: Record<string, unknown> }>('wifi/connect', {
      method: 'POST',
      query: { ssid, password },
      timeoutMs: WIFI_TIMEOUT_MS,
      acceptError: true,
    })
    return wifiFromApi(response.wifi)
  }

  async wifiReconnect(): Promise<PsuWifiStatus> {
    const response = await this.request<{ wifi: Record<string, unknown> }>('wifi/reconnect', {
      method: 'POST',
      timeoutMs: WIFI_TIMEOUT_MS,
      acceptError: true,
    })
    return wifiFromApi(response.wifi)
  }

  async wifiDisconnect(): Promise<PsuWifiStatus> {
    const response = await this.request<{ wifi: Record<string, unknown> }>('wifi/disconnect', { method: 'POST' })
    return wifiFromApi(response.wifi)
  }

  async wifiForget(): Promise<PsuWifiStatus> {
    const response = await this.request<{ wifi: Record<string, unknown> }>('wifi/forget', { method: 'POST' })
    return wifiFromApi(response.wifi)
  }

  async influxStatus(): Promise<PsuInfluxStatus> {
    const response = await this.request<{ influx: Record<string, unknown> }>('influx')
    return influxFromApi(response.influx)
  }

  async configureInflux(config: PsuInfluxConfig): Promise<PsuInfluxStatus> {
    const query: Record<string, ApiQueryValue> = {
      enabled: config.enabled,
      url: config.url,
      org: config.org,
      bucket: config.bucket,
      measurement: config.measurement,
      intervalMs: config.intervalMs,
    }
    if (config.token != null && config.token.length > 0) {
      query.token = config.token
    }
    if (config.clearToken) {
      query.clearToken = true
    }

    const response = await this.request<{ influx: Record<string, unknown> }>('influx', {
      method: 'POST',
      query,
    })
    return influxFromApi(response.influx)
  }

  async clearInterrupt(slot?: number): Promise<PsuCommandResponse> {
    const response = await this.request<Record<string, unknown>>('clear-interrupt', {
      method: 'POST',
      query: { slot },
    })
    return commandResponse('clear-interrupt', response)
  }

  async reset(target: ResetTarget, slot?: number): Promise<PsuCommandResponse> {
    const response = await this.request<Record<string, unknown>>('reset', {
      method: 'POST',
      query: { target, slot },
    })
    return commandResponse(`reset ${target}`, response)
  }

  private openMonitorStream(): void {
    if (typeof EventSource === 'undefined') {
      throw new PsuBackendError('EventSource monitor stream is not available in this browser')
    }

    const source = new EventSource(this.monitorStreamUrl())
    source.addEventListener('monitor', (event) => {
      this.handleMonitorEventData((event as MessageEvent<string>).data)
    })
    source.onmessage = (event) => {
      this.handleMonitorEventData(event.data)
    }
    source.onerror = () => {
      if (source !== this.monitorEvents) {
        return
      }
      source.close()
      this.monitorEvents = null
      if (this.monitorActive) {
        this.onMonitorStreamError?.(new PsuBackendError('Monitor event stream disconnected'))
      }
    }
    this.monitorEvents = source
  }

  private closeMonitorStream(): void {
    if (this.monitorEvents) {
      this.monitorEvents.close()
      this.monitorEvents = null
    }
  }

  private monitorStreamUrl(): string {
    const url = new URL(this.baseUrl)
    url.port = '81'
    url.pathname = '/api/v1/monitor/events'
    url.search = ''
    url.hash = ''
    // EventSource cannot set headers, so the session travels in the query.
    if (this.sessionToken != null) {
      url.searchParams.set('token', this.sessionToken)
    }
    return url.toString()
  }

  private handleMonitorEventData(data: string): void {
    const trimmed = data.trim()
    if (!trimmed) {
      return
    }

    let event: ApiMonitorEvent
    try {
      event = JSON.parse(trimmed) as ApiMonitorEvent
    } catch {
      return
    }

    const type = String(event.type ?? '').toLowerCase()
    if (type === 'hello' || type === 'config') {
      if (event.monitor) {
        this.applyMonitorState(monitorStateFromApi(objectField(event.monitor), this.monitorState(this.monitorActive)))
      }
      return
    }
    if (type === 'pins' && event.pins) {
      this.onPins?.(pinsFromApi(event.pins))
      return
    }
    if (type === 'cycle') {
      this.onMonitorCycle?.(monitorCycleFromEvent(event))
      return
    }
    if (type === 'register') {
      this.onRegisterUpdate?.(registerUpdateFromMonitorEvent(event))
      return
    }
    if (type === 'error') {
      return
    }
  }

  private applyMonitorState(state: PsuMonitorState): void {
    this.monitorActive = state.active
    this.monitorIntervalMs = state.intervalMs || this.monitorIntervalMs
    this.monitorSlot = state.slot as PsuMonitorSlot | null
    this.cadence = state.cadence
  }

  private monitorState(active: boolean): PsuMonitorState {
    return {
      slot: this.monitorSlot,
      active,
      intervalMs: this.monitorIntervalMs,
      count: this.monitorRegisters.length,
      cadence: this.cadence,
      raw: {
        ACTIVE: active ? '1' : '0',
        SLOT: this.monitorSlot == null ? '' : String(this.monitorSlot),
        INTERVAL_MS: String(this.monitorIntervalMs),
        COUNT: String(this.monitorRegisters.length),
        CADENCE: String(this.cadence),
      },
    }
  }

  private async request<T>(path: string, options: ApiRequestOptions = {}): Promise<T> {
    const url = new URL(`${this.baseUrl.replace(/\/$/, '')}/${path.replace(/^\//, '')}`)
    for (const [key, value] of Object.entries(options.query ?? {})) {
      if (value != null && value !== '') {
        url.searchParams.set(key, String(value))
      }
    }

    const method = options.method ?? 'GET'
    const controller = new AbortController()
    const timer = setTimeout(() => controller.abort(), options.timeoutMs ?? this.timeoutMs)
    const headers: Record<string, string> = {}
    if (!options.anonymous && this.sessionToken != null) {
      headers[AUTH_HEADER] = this.sessionToken
    }
    try {
      const response = await fetch(url, {
        method,
        cache: 'no-store',
        headers,
        signal: controller.signal,
      })
      const text = await response.text()
      const json = text.length > 0 ? JSON.parse(text) as Record<string, unknown> : {}
      if (response.status === 401 && json.result !== 'bad-password' && !options.anonymous) {
        // The session expired or the password changed elsewhere.
        this.forgetSession()
      }
      if (!response.ok && !options.acceptError) {
        throw apiError(json, response.status)
      }
      return json as T
    } catch (error) {
      if (error instanceof DOMException && error.name === 'AbortError') {
        throw new PsuBackendError(`Web API request timed out: ${url.pathname}`)
      }
      throw error
    } finally {
      clearTimeout(timer)
    }
  }

}

function apiError(json: Record<string, unknown>, status: number): PsuBackendError {
  const result = String(json.result ?? `http-${status}`)
  const message = String(json.message ?? 'Web API request failed')
  return new PsuBackendError(`${result}: ${message}`)
}

function firmwareInfoFromApi(raw: Record<string, unknown>): PsuFirmwareInfo {
  const wifi = objectField(raw.wifi)
  return {
    firmware: stringField(raw.firmware),
    protocol: numberField(raw.protocol),
    build: stringField(raw.build ?? '-'),
    cadence: stringField(raw.cadence ?? 'normal'),
    address: hexString(raw.address),
    detected: boolField(raw.detected),
    wifiStatus: stringField(wifi.status ?? 'unknown'),
    ip: stringField(wifi.ip ?? '-'),
    apiEnabled: boolField(objectField(raw.api).enabled ?? true),
    auth: raw.auth == null ? null : authStatusFromApi(raw.auth),
    influx: raw.influx == null ? null : influxFromApi(objectField(raw.influx)),
    raw: stringifyRecord(raw),
  }
}

function authStatusFromApi(raw: unknown): PsuAuthStatus {
  const value = objectField(raw)
  return {
    configured: boolField(value.configured),
    identity: stringField(value.identity ?? 'powerraid'),
    salt: stringField(value.salt ?? ''),
    iterations: numberField(value.iterations ?? 600000),
    telemetryPublic: boolField(value.telemetryPublic),
    lockoutMs: numberField(value.lockoutMs ?? 0),
    sessionTtlMs: numberField(value.sessionTtlMs ?? 0),
    passwordMinLength: numberField(value.passwordMinLength ?? 8),
    passwordMaxLength: numberField(value.passwordMaxLength ?? 128),
  }
}

function readSessionStore(): Record<string, string> {
  if (typeof window === 'undefined') {
    return {}
  }
  try {
    const parsed = JSON.parse(window.localStorage.getItem(SESSION_STORE_KEY) ?? '{}')
    return parsed != null && typeof parsed === 'object' ? parsed as Record<string, string> : {}
  } catch {
    return {}
  }
}

function slotSummaryFromApi(raw: ApiPins): PsuSlotSummary {
  const pins = pinsFromApi(raw)
  const slot = numberField(raw.slot)
  return {
    ...pins,
    slot,
    name: stringField(raw.name ?? `PSU${slot}`),
    address: hexString(raw.address),
    detected: boolField(raw.detected),
    rawLine: JSON.stringify(raw),
  }
}

function pinsFromApi(raw: ApiPins): PsuPins {
  return {
    slot: raw.slot == null ? null : numberField(raw.slot),
    presentN: boolField(raw.presentN),
    present: boolField(raw.present),
    psokHigh: boolField(raw.psok),
    psInterruptN: boolField(raw.psInterruptN),
    interruptAsserted: boolField(raw.interruptAsserted),
    psonAsserted: boolField(raw.psonAsserted),
    psonNPinHigh: boolField(raw.psonNPin),
    raw: stringifyRecord(raw as Record<string, unknown>),
    rawLines: [JSON.stringify(raw)],
  }
}

function probeFromApi(raw: Record<string, unknown>): PsuProbeResult {
  const probes = Array.isArray(raw.probes)
    ? raw.probes.map((probe) => probe as Record<string, unknown>)
    : [raw]
  const attempts: PsuProbeAttempt[] = probes.map((probe) => ({
    slot: probe.slot == null ? null : numberField(probe.slot),
    add: addressToAdd(probe.address),
    addBits: '-',
    address: hexString(probe.address),
    result: stringField(probe.result ?? (boolField(probe.ok) ? 'ok' : 'unknown')),
    ucInfo: probe.ucInfo == null ? null : hexString(probe.ucInfo),
    present: probe.present == null ? null : boolField(probe.present),
    raw: stringifyRecord(probe),
  }))
  const ok = boolField(raw.ok)
  const firstOk = attempts.find((attempt) => attempt.result === 'ok') ?? attempts[0]
  return {
    slot: raw.slot == null ? null : raw.slot === 'all' ? 'all' : numberField(raw.slot),
    ok,
    result: ok ? 'ok' : stringField(raw.result ?? firstOk?.result ?? 'unknown'),
    address: raw.address == null ? (firstOk?.address ?? null) : hexString(raw.address),
    ucInfo: raw.ucInfo == null ? (firstOk?.ucInfo ?? null) : hexString(raw.ucInfo),
    attempts,
    rawLines: [JSON.stringify(raw)],
  }
}

function addressFromApi(raw: Record<string, unknown>): PsuAddress {
  return {
    slot: raw.slot == null ? null : numberField(raw.slot),
    address: hexString(raw.address),
    detected: raw.detected == null ? null : boolField(raw.detected),
    raw: stringifyRecord(raw),
    rawLines: [JSON.stringify(raw)],
  }
}

function registerReadFromApi(raw: Record<string, unknown>): PsuRegisterRead {
  return {
    slot: raw.slot == null ? null : numberField(raw.slot),
    address: hexString(raw.address),
    register: hexString(raw.register),
    value: hexString(raw.value),
    raw: stringifyRecord(raw),
    rawLines: [JSON.stringify(raw)],
  }
}

function psonFromApi(raw: Record<string, unknown>): PsuPsonState {
  return {
    slot: raw.slot == null ? null : raw.slot === 'all' ? 'all' : numberField(raw.slot),
    asserted: boolField(raw.asserted),
    psonNPinHigh: boolField(raw.psonNPin),
    automatic: boolField(raw.automatic),
    powerPolicy: raw.powerPolicy == null ? null : powerPolicyFromApi(objectField(raw.powerPolicy)),
    raw: stringifyRecord(raw),
    rawLines: [JSON.stringify(raw)],
  }
}

function powerPolicySlotFromApi(raw: ApiPowerPolicySlot): PsuPowerPolicySlot {
  return {
    slot: numberField(raw.slot),
    present: boolField(raw.present),
    psonAsserted: boolField(raw.psonAsserted),
    psokHigh: boolField(raw.psok),
    statusResult: stringField(raw.statusResult ?? 'unknown'),
    outputPowerResult: stringField(raw.outputPowerResult ?? 'unknown'),
    psStatus: hexString(raw.psStatus ?? '0x0000'),
    inputOk: boolField(raw.inputOk),
    outputOk: boolField(raw.outputOk),
    psuPsonAsserted: boolField(raw.psuPsonAsserted),
    outputPowerW: numberField(raw.outputPowerW ?? 0),
    candidate: boolField(raw.candidate),
    failed: boolField(raw.failed),
    startupGrace: boolField(raw.startupGrace),
    raw: stringifyRecord(raw as Record<string, unknown>),
  }
}

function powerPolicyFromApi(raw: Record<string, unknown>): PsuPowerPolicy {
  const slots = Array.isArray(raw.slots)
    ? raw.slots.map((slot) => powerPolicySlotFromApi(slot as ApiPowerPolicySlot))
    : []
  const availableEfficiencyProfiles = Array.isArray(raw.availableEfficiencyProfiles)
    ? raw.availableEfficiencyProfiles.map((profile) => {
        const value = objectField(profile)
        return {
          id: stringField(value.id),
          label: stringField(value.label),
        }
      })
    : []
  return {
    automatic: boolField(raw.automatic),
    nRedundancy: numberField(raw.n_redundancy ?? raw.nRedundancy ?? 0),
    efficiencyProfile: stringField(raw.efficiencyProfile ?? 'datasheet-115v'),
    efficiencyProfileLabel: stringField(raw.efficiencyProfileLabel ?? raw.efficiencyProfile ?? 'Datasheet'),
    availableEfficiencyProfiles,
    psuWatts: numberField(raw.psuWatts ?? 900),
    presentCount: numberField(raw.presentCount ?? 0),
    candidateCount: numberField(raw.candidateCount ?? 0),
    activeCount: numberField(raw.activeCount ?? 0),
    targetActiveCount: numberField(raw.targetActiveCount ?? 0),
    nonRedundantCount: numberField(raw.nonRedundantCount ?? 0),
    maxOutputW: numberField(raw.maxOutputW ?? 0),
    totalOutputW: numberField(raw.totalOutputW ?? 0),
    telemetryValid: boolField(raw.telemetryValid),
    limitExceeded: boolField(raw.limitExceeded),
    redundancyShortfall: boolField(raw.redundancyShortfall),
    lastServiceMs: numberField(raw.lastServiceMs ?? 0),
    lastDecisionMs: numberField(raw.lastDecisionMs ?? 0),
    slots,
    raw: stringifyRecord(raw),
    rawLines: [JSON.stringify(raw)],
  }
}

function wifiFromApi(raw: Record<string, unknown>): PsuWifiStatus {
  const connected = boolField(raw.connected)
  return {
    status: stringField(raw.status ?? 'unknown'),
    connected,
    remembered: boolField(raw.remembered),
    hostname: stringField(raw.hostname ?? '-'),
    mdnsStarted: boolField(raw.mdns),
    ssid: raw.ssid == null || raw.ssid === '' ? null : stringField(raw.ssid),
    ip: raw.ip == null || raw.ip === '-' ? null : stringField(raw.ip),
    rssi: connected ? numberField(raw.rssi ?? 0) : null,
    apiEnabled: true,
    raw: stringifyRecord(raw),
    rawLines: [JSON.stringify(raw)],
  }
}

function influxFromApi(raw: Record<string, unknown>): PsuInfluxStatus {
  return {
    enabled: boolField(raw.enabled),
    configured: boolField(raw.configured),
    url: stringField(raw.url ?? ''),
    org: stringField(raw.org ?? 'powerraid'),
    bucket: stringField(raw.bucket ?? 'zigbee'),
    measurement: stringField(raw.measurement ?? 'powerraid_psu'),
    intervalMs: numberField(raw.intervalMs ?? 80),
    tokenSet: boolField(raw.tokenSet),
    lastResult: stringField(raw.lastResult ?? 'idle'),
    lastError: stringField(raw.lastError ?? ''),
    lastHttpStatus: numberField(raw.lastHttpStatus ?? 0),
    lastAttemptMs: numberField(raw.lastAttemptMs ?? 0),
    lastSuccessMs: numberField(raw.lastSuccessMs ?? 0),
    writeCount: numberField(raw.writeCount ?? 0),
    errorCount: numberField(raw.errorCount ?? 0),
    raw: stringifyRecord(raw),
    rawLines: [JSON.stringify(raw)],
  }
}

function monitorStateFromApi(raw: Record<string, unknown>, fallback: PsuMonitorState): PsuMonitorState {
  const slot = raw.slot == null ? fallback.slot : monitorSlotFromApi(raw.slot)
  const active = raw.active == null ? fallback.active : boolField(raw.active)
  const intervalMs = raw.intervalMs == null ? fallback.intervalMs : numberField(raw.intervalMs)
  const count = raw.count == null ? fallback.count : numberField(raw.count)
  const cadence = raw.cadence == null ? fallback.cadence : stringField(raw.cadence)
  return {
    slot,
    active,
    intervalMs,
    count,
    cadence,
    raw: {
      ACTIVE: active ? '1' : '0',
      SLOT: slot == null ? '' : String(slot),
      INTERVAL_MS: String(intervalMs),
      COUNT: String(count),
      CADENCE: String(cadence),
    },
    rawLines: [JSON.stringify(raw)],
  }
}

function monitorSlotFromApi(value: unknown): PsuMonitorState['slot'] {
  if (value == null || value === '') {
    return null
  }
  const text = String(value).toLowerCase()
  if (text === 'all') {
    return 'all'
  }
  const slot = numberField(value)
  return slot >= 1 && slot <= 3 ? slot : null
}

function registerSlotFromApi(value: unknown): number | null {
  const slot = monitorSlotFromApi(value)
  return typeof slot === 'number' ? slot : null
}

function monitorCycleFromEvent(event: ApiMonitorEvent): PsuMonitorCycle {
  const phaseText = String(event.phase ?? 'unknown').toLowerCase()
  const phase: PsuMonitorCycle['phase'] = phaseText === 'start' || phaseText === 'end' ? phaseText : 'unknown'
  const raw = {
    CYCLE: String(event.cycle ?? ''),
    SLOT: event.slot == null ? '' : String(event.slot),
    START: phase === 'start' ? '1' : '',
    END: phase === 'end' ? '1' : '',
    MS: event.ms == null ? '' : String(event.ms),
  }
  return {
    slot: monitorSlotFromApi(event.slot),
    cycle: event.cycle == null ? 0 : numberField(event.cycle),
    phase,
    ms: event.ms == null ? null : numberField(event.ms),
    raw,
  }
}

function registerUpdateFromMonitorEvent(event: ApiMonitorEvent): PsuRegisterUpdate {
  const result = stringField(event.result ?? (event.value ? 'ok' : 'unknown')).toLowerCase()
  const reg = hexString(event.register)
  const value = event.value == null ? null : hexString(event.value)
  const slot = registerSlotFromApi(event.slot)
  const raw = {
    CYCLE: String(event.cycle ?? ''),
    SLOT: slot == null ? '' : String(slot),
    ADDRESS: hexString(event.address),
    REG: reg,
    RESULT: result,
    VALUE: value ?? '',
    MS: event.ms == null ? String(Date.now()) : String(event.ms),
  }
  const rawLine = `REG CYCLE=${raw.CYCLE} SLOT=${raw.SLOT} ADDRESS=${raw.ADDRESS} REG=${raw.REG} MS=${raw.MS} RESULT=${raw.RESULT}${value == null ? '' : ` VALUE=${value}`}`
  return {
    slot,
    cycle: event.cycle == null ? null : numberField(event.cycle),
    address: raw.ADDRESS,
    register: reg,
    registerAddress: u8Value(reg),
    value,
    result,
    ms: Number(raw.MS),
    ok: result === 'ok' && value != null,
    raw,
    rawLine,
  }
}

function commandResponse(command: string, raw: Record<string, unknown>): PsuCommandResponse {
  const ok = raw.ok !== false
  return {
    command,
    lines: [JSON.stringify(raw)],
    ok,
    terminalLine: ok ? `OK ${command}` : `ERR ${command}`,
  }
}

function objectField(value: unknown): Record<string, unknown> {
  return value != null && typeof value === 'object' && !Array.isArray(value)
    ? value as Record<string, unknown>
    : {}
}

function stringField(value: unknown): string {
  return value == null ? '?' : String(value)
}

function numberField(value: unknown): number {
  const number = Number(value)
  if (!Number.isFinite(number)) {
    throw new PsuBackendError(`Web API field is not numeric: ${String(value)}`)
  }
  return number
}

function boolField(value: unknown): boolean {
  return value === true || value === 1 || value === '1' || value === 'true'
}

function hexString(value: unknown): string {
  return stringField(value).replace(/^0X/, '0x')
}

function hex8(value: number): string {
  return `0x${value.toString(16).padStart(2, '0')}`
}

function hex16(value: number): string {
  return `0x${value.toString(16).padStart(4, '0')}`
}

function addressToAdd(address: unknown): number {
  try {
    return u8Value(hexString(address)) - 0x58
  } catch {
    return -1
  }
}

function stringifyRecord(raw: Record<string, unknown>): Record<string, string> {
  return Object.fromEntries(Object.entries(raw).map(([key, value]) => [key, String(value)]))
}
