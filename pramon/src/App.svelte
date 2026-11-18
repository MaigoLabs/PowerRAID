<!-- SPDX-License-Identifier: Apache-2.0 -->
<script lang="ts">
  import { onMount } from 'svelte'
  import ActionsCard from '$lib/pramon/components/ActionsCard.svelte'
  import AppHeader from '$lib/pramon/components/AppHeader.svelte'
  import BridgeCard from '$lib/pramon/components/BridgeCard.svelte'
  import ConnectionCard from '$lib/pramon/components/ConnectionCard.svelte'
  import EventsCard from '$lib/pramon/components/EventsCard.svelte'
  import FirstRunPasswordDialog from '$lib/pramon/components/FirstRunPasswordDialog.svelte'
  import InfluxCard from '$lib/pramon/components/InfluxCard.svelte'
  import PowerPolicyCard from '$lib/pramon/components/PowerPolicyCard.svelte'
  import RegistersCard from '$lib/pramon/components/RegistersCard.svelte'
  import SecurityCard from '$lib/pramon/components/SecurityCard.svelte'
  import StatusPinsCard from '$lib/pramon/components/StatusPinsCard.svelte'
  import TelemetryCard from '$lib/pramon/components/TelemetryCard.svelte'
  import TelemetryDialog from '$lib/pramon/components/TelemetryDialog.svelte'
  import WifiCard from '$lib/pramon/components/WifiCard.svelte'
  import type {
    ChartPoint,
    PsControlActionRow,
    PsControlSystemTypeRow,
    RegisterTableEntry,
    RegisterTableRow,
    TelemetryMetricRow,
    TelemetryPoint,
    TelemetrySample,
    TelemetrySeriesKey,
    TelemetrySeriesPath,
    Tone,
  } from '$lib/pramon/types'
  import {
    REGISTER_OPTIONS,
    DEFAULT_MONITOR_REGISTERS,
    createPsuBackend,
    defaultWebApiBaseUrl,
    normalizeWebApiBaseUrl,
    registerKeyForAddress,
    registerWriteValidationError,
    statusFromRegisterValues,
    type PsonCommand,
    type PsuCadenceMode,
    type PsuAddress,
    type PsuAuthStatus,
    type PsuBackend,
    type PsuFirmwareInfo,
    type PsuInfluxStatus,
    type PsuMonitorCycle,
    type PsuMonitorState,
    type PsuPins,
    type PsuPowerPolicy,
    type PsuPsonState,
    type PsuRegisterRead,
    type PsuRegisterUpdate,
    type PsuSlotSelector,
    type PsuSlotSummary,
    type PsuStatus,
    type PsuWifiStatus,
    type RegisterOption,
  } from './lib/psu'

  const RW_REGISTER_OPTIONS = REGISTER_OPTIONS.filter((option) => option.access === 'RW')
  const REGISTER_TABLE = [...REGISTER_OPTIONS].sort((left, right) => left.address - right.address)
  const DEFAULT_LISTENED_REGISTER_ADDRESSES = new Set<number>(DEFAULT_MONITOR_REGISTERS)
  const DEFAULT_LISTENED_REGISTER_KEYS = new Set(
    REGISTER_TABLE
      .filter((option) => DEFAULT_LISTENED_REGISTER_ADDRESSES.has(option.address))
      .map((option) => option.value),
  )
  const DEFAULT_LISTENED_REGISTERS = Object.fromEntries(
    REGISTER_TABLE.map((option) => [option.value, DEFAULT_LISTENED_REGISTER_KEYS.has(option.value)]),
  ) as Record<string, boolean>
  const MAX_TELEMETRY_HISTORY = 72
  const SPARKLINE_WINDOW_MS = 20_000
  const MONITOR_RETRY_DELAYS_MS = [1_000, 2_000, 5_000, 10_000, 30_000] as const
  const RESET_ONLY_REGISTER_ADDRESSES = new Set([0x30, 0x32, 0x34, 0x36])
  const FAN_RPM_REGISTER_ADDRESSES = new Set([0x3c, 0x3e])
  const CURRENT_LIMIT_REGISTER_ADDRESSES = new Set([0x5a, 0x5c])
  const PS_CONTROL_ACTIONS = [
    { label: 'Shutdown IRQ', bit: 3 },
    { label: 'Warning IRQ', bit: 4 },
    { label: 'Clear IRQ', bit: 5, clearOnly: true },
    { label: 'Fan1 auto', bit: 6, clearOnly: true },
    { label: 'Fan2 auto', bit: 7, clearOnly: true },
    { label: 'LED standby', bit: 8 },
    { label: 'Fan-fault ignore', bit: 9, caution: true },
    { label: 'Vout step up', bit: 10, caution: true },
    { label: 'Adjust Vout', bit: 11, caution: true },
    { label: 'Vout step down', bit: 12, caution: true },
    { label: 'Standby save', bit: 13, caution: true },
  ] as const
  const PS_CONTROL_SYSTEM_TYPES = [
    { label: 'ML/DL', value: 0, caution: false },
    { label: 'Blade', value: 1, caution: false },
    { label: 'Redundant fan', value: 2, caution: true },
    { label: 'Step up', value: 3, caution: true },
    { label: 'Adjust Vout', value: 4, caution: true },
    { label: 'Step down', value: 5, caution: true },
  ] as const
  const DEFAULT_PSU_SLOTS: PsuSlotSummary[] = [1, 2, 3].map((slot) => ({
    slot,
    name: `PSU${slot}`,
    address: `0x${(0x57 + slot).toString(16).toUpperCase()}`,
    detected: false,
    presentN: true,
    present: false,
    psokHigh: false,
    psInterruptN: true,
    interruptAsserted: false,
    psonAsserted: false,
    psonNPinHigh: true,
    raw: {},
    rawLine: '',
  }))
  const PSU_SLOT_IDS = [1, 2, 3] as const
  const TELEMETRY_SLOT_COLORS: Record<number, string> = {
    1: '#2563eb',
    2: '#16a34a',
    3: '#dc2626',
  }
  const EMPTY_SLOT_REGISTER_VALUES: Record<number, Record<string, string>> = Object.fromEntries(
    PSU_SLOT_IDS.map((slot) => [slot, {}]),
  ) as Record<number, Record<string, string>>
  const EMPTY_SLOT_REGISTER_ENTRIES: Record<number, Record<string, RegisterTableEntry>> = Object.fromEntries(
    PSU_SLOT_IDS.map((slot) => [slot, {}]),
  ) as Record<number, Record<string, RegisterTableEntry>>
  const EMPTY_TELEMETRY_HISTORY_BY_SLOT: Record<number, TelemetrySample[]> = Object.fromEntries(
    PSU_SLOT_IDS.map((slot) => [slot, []]),
  ) as Record<number, TelemetrySample[]>

  type TelemetryHistorySummary = {
    slot: number
    sampleCount: number
    latestTimestamp: number | null
  }

  let webApiBaseUrl = defaultWebApiBaseUrl()
  let backendConfigKey = ''
  let desiredBackendConfigKey = ''
  let backend: PsuBackend = createConfiguredBackend()
  let connected = false
  let busy = ''
  let errorMessage = ''
  let firmwareInfo: PsuFirmwareInfo | null = null
  let selectedSlot = 1
  let psuSlots: PsuSlotSummary[] = DEFAULT_PSU_SLOTS
  let monitor: PsuMonitorState | null = null
  let status: PsuStatus | null = null
  let pins: PsuPins | null = null
  let pson: PsuPsonState | null = null
  let powerPolicy: PsuPowerPolicy | null = null
  let wifi: PsuWifiStatus | null = null
  let influx: PsuInfluxStatus | null = null
  let wifiSsid = ''
  let wifiPassword = ''
  let showWifiPassword = false
  let influxEnabled = false
  let influxUrl = ''
  let influxOrg = 'powerraid'
  let influxBucket = 'zigbee'
  let influxMeasurement = 'powerraid_psu'
  let influxIntervalMs = '80'
  let influxToken = ''
  let auth: PsuAuthStatus | null = null
  let showPassword = false
  let passwordField = ''
  let confirmField = ''
  let passwordError = ''
  let firstRunDismissed = false
  let showFirstRunPrompt = false
  let readResult: PsuRegisterRead | null = null
  let addressResult: PsuAddress | null = null
  let registerValues: Record<string, string> = {}
  let registerTableEntries: Record<string, RegisterTableEntry> = {}
  let slotRegisterValues: Record<number, Record<string, string>> = { ...EMPTY_SLOT_REGISTER_VALUES }
  let slotRegisterTableEntries: Record<number, Record<string, RegisterTableEntry>> = { ...EMPTY_SLOT_REGISTER_ENTRIES }
  let slotStatuses: Record<number, PsuStatus> = {}
  let slotPins: Record<number, PsuPins> = {}
  let listenedRegisters: Record<string, boolean> = { ...DEFAULT_LISTENED_REGISTERS }
  let registerWriteValues: Record<string, string> = Object.fromEntries(
    RW_REGISTER_OPTIONS.map((option) => [option.value, '']),
  )
  let autoRefreshEnabled = true
  let autoRefreshMs = '1000'
  let fastCadence = false
  let lastCommandMs: number | null = null
  let monitorSyncTimer: ReturnType<typeof setTimeout> | null = null
  let lastMonitorTarget = ''
  let lastAppliedCadence: PsuCadenceMode | null = null
  let monitorRetryAttempt = 0
  let lastSampledCycleBySlot: Record<number, number> = {}
  let displayClockMs = Date.now()
  let expandedTelemetryKey: TelemetrySeriesKey | null = null
  let telemetryHistoryBySlot: Record<number, TelemetrySample[]> = { ...EMPTY_TELEMETRY_HISTORY_BY_SLOT }
  let telemetryHistorySummary: TelemetryHistorySummary[] = []
  let telemetryMetrics: TelemetryMetricRow[] = []

  $: telemetry = status?.telemetry ?? null
  $: telemetryHistory = telemetryHistoryBySlot[selectedSlot] ?? []
  $: telemetryHistorySummary = PSU_SLOT_IDS.map((slot) => {
    const history = telemetryHistoryBySlot[slot] ?? []
    return {
      slot,
      sampleCount: history.length,
      latestTimestamp: history.at(-1)?.timestamp ?? null,
    }
  })
  $: allTelemetrySampleCount = telemetryHistorySummary.reduce((count, slot) => count + slot.sampleCount, 0)
  $: lastTelemetrySampleAt = latestTelemetryTimestamp(telemetryHistorySummary)
  $: transportName = 'Web API'
  $: webApiInputMissing = webApiBaseUrl.trim().length === 0
  $: autoRefreshIntervalMs = Number.parseInt(autoRefreshMs, 10)
  $: safeAutoRefreshIntervalMs = Number.isFinite(autoRefreshIntervalMs)
    ? Math.max(1_000, autoRefreshIntervalMs)
    : 1_000
  $: connectionState = webApiInputMissing
    ? 'unavailable'
    : busy
      ? 'busy'
      : connected
        ? 'connected'
        : 'disconnected'
  $: connectionLabel = connectionState === 'unavailable'
    ? `${transportName} unavailable`
    : connectionState === 'busy'
      ? `Busy - ${busy}`
      : connected
        ? 'Connected'
        : 'Disconnected'
  $: connectionTone = connectionState === 'unavailable'
    ? 'bad'
    : connectionState === 'busy'
      ? 'warn'
      : connected
        ? 'good'
        : 'muted' as Tone
  // These mirror backend getters, which Svelte cannot track; syncAccess() is
  // called wherever the backend's session or connection state can change.
  let authenticated = false
  let canRead = false
  let canWrite = false
  // First access to an open bridge asks for a password once per browser.
  $: showFirstRunPrompt =
    connected &&
    auth != null &&
    !auth.configured &&
    !firstRunDismissed &&
    !window.localStorage.getItem('pramon.passwordPromptShown')
  $: connectionDescription = webApiInputMissing
      ? 'Enter bridge API base URL'
      : errorMessage || `${transportName} transport`
  $: statusSubtitle = status
    ? `${selectedSlotLabel} · FW ${status.firmware} · ${vendorName(status.vendor)} · ${lastCommandMs == null ? 'sampled' : `${formatNumber(lastCommandMs / 1000, 2)}s sample`}`
    : `${selectedSlotLabel} · no status sample`
  $: telemetrySubtitle = lastTelemetrySampleAt == null
    ? 'Waiting for first sample'
    : `${allTelemetrySampleCount} samples across slots · last ${timeAgo(lastTelemetrySampleAt)}`
  $: transportDetailLabel = 'Endpoint'
  $: transportDetail = webApiEndpointLabel(webApiBaseUrl)
  $: bridgeFirmwareLabel = firmwareInfo ? formatBridgeFirmwareLabel(firmwareInfo) : 'unknown'
  $: listenedRegisterAddresses = REGISTER_TABLE
    .filter((register) => listenedRegisters[register.value])
    .map((register) => register.address)
  $: monitorRegisterAddresses = Array.from(new Set([0x00, 0x02, 0x3a, ...listenedRegisterAddresses]))
    .sort((left, right) => left - right)
  $: listenedRegisterCount = listenedRegisterAddresses.length
  $: registerRows = REGISTER_TABLE.map((register): RegisterTableRow => {
    const entry = registerTableEntries[register.value]
    const stale = entry != null && displayClockMs - entry.updatedAt > 30_000
    return {
      register,
      listened: listenedRegisters[register.value] ?? false,
      liveValue: entry?.value ?? registerValues[register.value] ?? '-',
      parsedValue: parseRegisterDisplay(register, entry?.value ?? registerValues[register.value] ?? '-'),
      updateText: entry == null ? '-' : formatTimestamp(entry.updatedAt),
      updateTone: entry == null || stale ? 'muted' : entry.result === 'ok' ? 'default' : 'warn',
    }
  })
  $: selectedAddress = status?.address ?? addressResult?.address ?? selectedSlotSummary?.address ?? firmwareInfo?.address ?? '-'
  $: psControlRaw = status?.psControl ?? registerValues['ps-control'] ?? registerTableEntries['ps-control']?.value ?? ''
  $: psControlWord = parseRegisterWord(psControlRaw)
  $: psControlSystemType = psControlWord == null ? null : psControlWord & 0x0007
  $: psControlActionRows = PS_CONTROL_ACTIONS.map((action): PsControlActionRow => {
    const active = psControlWord != null && (psControlWord & (1 << action.bit)) !== 0
    return {
      ...action,
      active,
      disabled: !connected || busy !== '' || psControlWord == null || (psControlActionClearOnly(action) && !active),
      className: psControlActionClass(action, active),
    }
  })
  $: psControlSystemTypeRows = PS_CONTROL_SYSTEM_TYPES.map((systemType): PsControlSystemTypeRow => {
    const active = psControlSystemType === systemType.value
    return {
      ...systemType,
      active,
      disabled: !connected || busy !== '' || psControlWord == null,
      className: psControlSystemTypeClass(systemType, active),
    }
  })
  $: selectedSlotSummary = psuSlots.find((slot) => slot.slot === selectedSlot) ?? DEFAULT_PSU_SLOTS[selectedSlot - 1]
  $: selectedSlotLabel = selectedSlotSummary?.name ? selectedSlotSummary.name.replace(/^PSU/, 'PSU ') : `PSU ${selectedSlot}`
  $: selectedSlotStatus = selectedSlotSummary?.present
    ? selectedSlotSummary.psokHigh
      ? 'Present · PSOK'
      : 'Present · not OK'
    : 'Not present'
  $: monitorTarget = connected
    ? `all:${autoRefreshEnabled ? safeAutoRefreshIntervalMs : 0}:${fastCadence ? 'fast' : 'normal'}:${monitorRegisterAddresses.join(',')}`
    : 'disconnected'
  $: psonDriveAsserted = pson?.asserted ?? pins?.psonAsserted ?? false
  $: psonToggleLabel = psonDriveAsserted ? 'PSON off' : 'PSON on'
  $: psonToggleCommand = (psonDriveAsserted ? 'off' : 'on') as PsonCommand
  $: powerPolicyDescription = powerPolicy
    ? `${powerPolicy.activeCount}/${powerPolicy.presentCount} active · ${formatPowerWatts(powerPolicy.totalOutputW)}`
    : 'MCU automatic control'
  $: wifiStatusLabel = !connected
    ? 'Bridge disconnected'
    : wifi == null
      ? 'Status unknown'
      : wifi.connected
        ? 'Connected'
        : wifiStatusDisplay(wifi.status)
  $: wifiStatusTone = !connected || wifi == null
    ? 'muted' as Tone
    : wifi.connected
      ? 'good' as Tone
      : wifi.status === 'connect-failed' || wifi.status === 'no-ssid' || wifi.status === 'connection-lost'
        ? 'bad' as Tone
        : 'warn' as Tone
  $: desiredBackendConfigKey = webApiBaseUrl
  $: if (!connected && desiredBackendConfigKey !== backendConfigKey) {
    backendConfigKey = desiredBackendConfigKey
    backend = createConfiguredBackend()
  }
  $: statusMetrics = [
    {
      label: 'Input',
      value: status ? stateWord(status.inputOk, 'OK', 'Fault') : 'n/a',
      detail: 'INPUT bit',
      tone: status ? toneFromBool(status.inputOk) : 'muted' as Tone,
    },
    {
      label: 'Output',
      value: status ? stateWord(status.outputOk, 'OK', 'Off') : 'n/a',
      detail: '12 V rail',
      tone: status ? (status.outputOk ? 'good' : 'warn') : 'muted' as Tone,
    },
    {
      label: 'PSON#',
      value: status ? stateWord(status.psuPsonAsserted, 'On', 'Off') : 'n/a',
      detail: 'active low',
      tone: status ? (status.psuPsonAsserted ? 'good' : 'warn') : 'muted' as Tone,
    },
    {
      label: 'Range',
      value: status?.range ?? 'n/a',
      detail: 'input range',
      tone: status ? 'default' as Tone : 'muted' as Tone,
    },
    {
      label: 'Firmware',
      value: status?.firmware ?? 'n/a',
      detail: status ? ucTypeName(status.ucType) : 'uC',
      tone: status ? 'default' as Tone : 'muted' as Tone,
    },
    {
      label: 'Address',
      value: status?.address ?? addressResult?.address ?? 'n/a',
      detail: 'selected monitor',
      tone: status || addressResult ? 'default' as Tone : 'muted' as Tone,
    },
  ]
  $: telemetryMetrics = buildTelemetryMetrics(telemetryHistoryBySlot, selectedSlot)
  $: expandedTelemetryMetric = expandedTelemetryKey == null
    ? null
    : (telemetryMetrics.find((metric) => metric.key === expandedTelemetryKey) ?? null)
  $: eventRows = [
    {
      label: 'Shutdown event',
      value: status?.shutdownEvent ?? 'n/a',
      tone: eventTone(status?.shutdownEvent, 'bad'),
    },
    {
      label: 'Warning event',
      value: status?.warningEvent ?? 'n/a',
      tone: eventTone(status?.warningEvent, 'warn'),
    },
    {
      label: 'Interrupt latch',
      value: status ? stateWord(status.interruptLatched, 'Latched', 'Clear') : 'n/a',
      tone: status ? (status.interruptLatched ? 'bad' : 'good') : 'muted' as Tone,
    },
    {
      label: 'PS Control',
      value: status ? psControlSummary(status.psControl) : 'n/a',
      tone: status ? 'default' as Tone : 'muted' as Tone,
    },
  ]
  $: pinRows = [
    {
      label: 'PRESENT#',
      value: pins ? stateWord(pins.present, 'Low - present', 'High - absent') : 'n/a',
      detail: 'sideband',
      tone: pins ? (pins.present ? 'good' : 'warn') : 'muted' as Tone,
    },
    {
      label: 'PSOK',
      value: pins ? stateWord(pins.psokHigh, 'High', 'Low') : 'n/a',
      detail: 'sideband',
      tone: pins ? (pins.psokHigh ? 'good' : 'warn') : 'muted' as Tone,
    },
    {
      label: 'PS_INTERRUPT',
      value: pins ? stateWord(pins.interruptAsserted, 'Asserted low', 'Inactive high') : 'n/a',
      detail: 'sideband',
      tone: pins ? (pins.interruptAsserted ? 'bad' : 'good') : 'muted' as Tone,
    },
  ]

  $: if (monitorTarget !== lastMonitorTarget) {
    lastMonitorTarget = monitorTarget
    monitorRetryAttempt = 0
    queueMonitorSync()
  }

  onMount(() => {
    const clockTimer = setInterval(() => {
      displayClockMs = Date.now()
    }, 1_000)
    return () => {
      clearInterval(clockTimer)
      stopMonitorSyncTimer()
    }
  })

  function createConfiguredBackend() {
    return createPsuBackend({
      webapi: {
        baseUrl: webApiBaseUrl,
        onPins: handlePinsUpdate,
        onRegisterUpdate: handleRegisterUpdate,
        onMonitorCycle: handleMonitorCycle,
        onMonitorStreamError: handleMonitorStreamError,
      },
    })
  }

  async function connect() {
    if (webApiInputMissing) {
      errorMessage = 'Enter the bridge API base URL'
      return
    }

    // Connecting only reads, so it must work before any session exists.
    const ok = await run('Connect', async () => {
      await backend.connect()
      syncConnected()
      // A locked bridge refuses /info, and that must not look like a broken
      // connection; the unlock form appears instead.
      return backend.authStatus().catch(() => null)
    }, (result) => {
      auth = result
    }, 'none')
    if (ok) {
      const status = auth
      const locked = status?.configured === true && !backend.authenticated
      if (locked) {
        errorMessage = status.telemetryPublic
          ? 'Readings are visible, but settings need the bridge password'
          : 'This bridge is password protected; unlock it to continue'
      } else {
        await syncBridgeDetails()
      }
      monitorRetryAttempt = 0
      queueMonitorSync()
    }
  }

  async function disconnect() {
    stopMonitorSyncTimer()
    busy = 'Disconnect'
    errorMessage = ''
    try {
      if (backend.connected) {
        await backend.stopMonitor().catch(() => undefined)
      }
      await backend.disconnect()
      setConnected(false)
      firmwareInfo = null
      psuSlots = DEFAULT_PSU_SLOTS
      monitor = null
      lastAppliedCadence = null
      monitorRetryAttempt = 0
      status = null
      pins = null
      pson = null
      powerPolicy = null
      wifi = null
      influx = null
      readResult = null
      addressResult = null
      registerValues = {}
      registerTableEntries = {}
      slotRegisterValues = freshSlotRegisterValues()
      slotRegisterTableEntries = freshSlotRegisterEntries()
      slotStatuses = {}
      slotPins = {}
      telemetryHistoryBySlot = freshTelemetryHistoryBySlot()
      lastSampledCycleBySlot = {}
      auth = null
      passwordField = ''
      confirmField = ''
      passwordError = ''
      resetInfluxForm()
    } catch (error) {
      handleError(error)
    } finally {
      busy = ''
    }
  }

  async function refreshPsuSlots(silent = false) {
    if (!connected || !backend.connected) {
      return false
    }

    const remember = (result: PsuSlotSummary[]) => {
      psuSlots = mergePsuSlots(result)
    }

    if (silent) {
      try {
        remember(await backend.slots())
        return true
      } catch {
        return false
      }
    }

    return run('PSU slots', () => backend.slots(), remember, 'read')
  }

  async function refreshPowerPolicy(silent = false) {
    if (!connected || !backend.connected) {
      return false
    }

    const remember = (result: PsuPowerPolicy) => {
      powerPolicy = result
    }

    if (silent) {
      try {
        remember(await backend.powerPolicy())
        return true
      } catch {
        return false
      }
    }

    return run('Power policy', () => backend.powerPolicy(), remember, 'read')
  }

  async function selectPsuSlot(slot: number) {
    if (slot === selectedSlot || busy) {
      return
    }

    stopMonitorSyncTimer()
    if (connected && backend.connected) {
      await backend.stopMonitor().catch(() => undefined)
    }

    selectedSlot = slot
    monitor = null
    status = null
    pins = null
    pson = null
    readResult = null
    addressResult = null
    loadSelectedSlotState()

    if (connected) {
      await refreshPsuSlots(true)
      queueMonitorSync()
    }
  }

  function mergePsuSlots(result: PsuSlotSummary[]) {
    return DEFAULT_PSU_SLOTS.map((fallback) => {
      const next = result.find((slot) => slot.slot === fallback.slot)
      return next ? { ...fallback, ...next } : fallback
    })
  }

  function freshSlotRegisterValues() {
    return Object.fromEntries(PSU_SLOT_IDS.map((slot) => [slot, {}])) as Record<number, Record<string, string>>
  }

  function freshSlotRegisterEntries() {
    return Object.fromEntries(PSU_SLOT_IDS.map((slot) => [slot, {}])) as Record<number, Record<string, RegisterTableEntry>>
  }

  function freshTelemetryHistoryBySlot() {
    return Object.fromEntries(PSU_SLOT_IDS.map((slot) => [slot, []])) as Record<number, TelemetrySample[]>
  }

  function loadSelectedSlotState() {
    registerValues = slotRegisterValues[selectedSlot] ?? {}
    registerTableEntries = slotRegisterTableEntries[selectedSlot] ?? {}
    status = slotStatuses[selectedSlot] ?? null
    pins = slotPins[selectedSlot] ?? null
  }

  function selectFirstAvailableSlot() {
    const current = psuSlots.find((slot) => slot.slot === selectedSlot)
    if (current?.present || current?.detected) {
      return
    }
    const available = psuSlots.find((slot) => slot.present || slot.detected)
    if (available) {
      selectedSlot = available.slot
    }
  }

  async function readRegisterValue(register: RegisterOption) {
    await run(`Read ${selectedSlotLabel} ${register.label}`, () => backend.readRegister(register.value, selectedSlot), (result) => {
      readResult = result
      rememberSlotRegisterValue(result.slot ?? selectedSlot, register.value, result.value, 'ok', null)
      rebuildStatusFromRegisters(result.address, result.rawLines, result.slot ?? selectedSlot)
    }, 'read')
  }

  async function writeRegisterValue(event: SubmitEvent, register: RegisterOption) {
    event.preventDefault()
    if (register.access !== 'RW') {
      errorMessage = 'Only known RW LiteOn registers can be written from this UI'
      return
    }
    const value = registerWriteValues[register.value] ?? ''
    let parsed: { raw: number; display: string } | null = null
    try {
      parsed = parseRegisterWriteInput(register, value)
    } catch {
      return
    }
    if (!parsed) {
      return
    }
    if (
      !window.confirm(
        `Write ${register.label} ${registerAddress(register)} = ${parsed.display} (${hexWord(parsed.raw)})? This can alter PSU control, voltage, fan, OCP, or persistent state.`,
      )
    ) {
      return
    }

    const ok = await performRegisterWrite(register, parsed.raw, `Write ${register.label}`)
    if (ok && connected) {
      queueMonitorSync()
    }
  }

  async function resetRegisterValue(register: RegisterOption) {
    if (!isResetOnlyRegister(register)) {
      errorMessage = `${register.label} is not a reset-only register`
      return
    }
    const parsed = parseRegisterWriteInput(register, '0')
    if (!parsed) {
      return
    }
    if (!window.confirm(`Reset ${register.label} ${registerAddress(register)} by writing ${hexWord(parsed.raw)}?`)) {
      return
    }

    const ok = await performRegisterWrite(register, parsed.raw, `Reset ${register.label}`)
    if (ok && connected) {
      queueMonitorSync()
    }
  }

  async function performRegisterWrite(register: RegisterOption, raw: number, label: string) {
    return run(label, () => backend.writeRegister(register.value, raw, selectedSlot), (result) => {
      readResult = result
      rememberSlotRegisterValue(result.slot ?? selectedSlot, register.value, result.value, 'ok', null)
      rebuildStatusFromRegisters(result.address, result.rawLines, result.slot ?? selectedSlot)
    })
  }

  async function setPson(command: PsonCommand, slot: PsuSlotSelector = selectedSlot, label = `${selectedSlotLabel} PSON ${command}`) {
    if (command === 'on' && !window.confirm('Assert PSON# low and energize the main 12 V rail?')) {
      return
    }
    if (command === 'off' && !window.confirm('Release PSON# and shut down the main 12 V rail?')) {
      return
    }

    const ok = await run(label, () => backend.pson(command, slot), (result) => {
      pson = result
      if (result.powerPolicy) {
        powerPolicy = result.powerPolicy
      }
    })
    if (ok && connected) {
      await refreshPsuSlots(true)
      await refreshPowerPolicy(true)
      queueMonitorSync()
    }
  }

  async function togglePson() {
    await setPson(psonToggleCommand)
  }

  async function setPowerPolicyOff() {
    if (!window.confirm('Release PSON# for all PSU slots and keep automatic control off?')) {
      return
    }
    const ok = await run('Power policy off', () => backend.pson('off', 'all'), (result) => {
      pson = result
      if (result.powerPolicy) {
        powerPolicy = result.powerPolicy
      }
    })
    if (ok && connected) {
      await refreshPsuSlots(true)
      await refreshPowerPolicy(true)
      queueMonitorSync()
    }
  }

  async function setPowerPolicyAutomatic(automatic: boolean) {
    if (automatic && !window.confirm('Enable MCU automatic PSON control?')) {
      return
    }
    const ok = await run('Power policy', () => backend.powerPolicy({ automatic }), (result) => {
      powerPolicy = result
    })
    if (ok && connected) {
      await refreshPsuSlots(true)
      queueMonitorSync()
    }
  }

  async function setPowerPolicyRedundancy(nRedundancy: number) {
    const ok = await run(`n_redundancy ${nRedundancy}`, () => backend.powerPolicy({ nRedundancy }), (result) => {
      powerPolicy = result
    })
    if (ok && connected) {
      await refreshPsuSlots(true)
      queueMonitorSync()
    }
  }

  async function setPowerPolicyEfficiencyProfile(efficiencyProfile: string) {
    const ok = await run('Efficiency profile', () => backend.powerPolicy({ efficiencyProfile }), (result) => {
      powerPolicy = result
    })
    if (ok && connected) {
      await refreshPowerPolicy(true)
    }
  }

  async function clearInterrupt() {
    const ok = await run(`Clear ${selectedSlotLabel} interrupt`, () => backend.clearInterrupt(selectedSlot))
    if (ok && connected) {
      queueMonitorSync()
    }
  }

  async function refreshWifiStatus(silent = false) {
    if (!connected || !backend.connected) {
      return false
    }

    if (silent) {
      try {
        rememberWifiStatus(await backend.wifiStatus())
        return true
      } catch {
        wifi = null
        return false
      }
    }

    return run('WiFi status', () => backend.wifiStatus(), rememberWifiStatus, 'read')
  }

  async function connectWifi(event: SubmitEvent) {
    event.preventDefault()
    const ssid = wifiSsid
    if (ssid.length === 0) {
      errorMessage = 'Enter a WiFi SSID'
      return
    }

    const ok = await run(
      'WiFi connect',
      () => backend.wifiConnect(ssid, wifiPassword),
      rememberWifiStatus,
    )
    if (ok) {
      wifiPassword = ''
    }
  }

  async function reconnectWifi() {
    await run('WiFi reconnect', () => backend.wifiReconnect(), rememberWifiStatus)
  }

  async function disconnectWifi() {
    await run('WiFi disconnect', () => backend.wifiDisconnect(), rememberWifiStatus)
  }

  async function forgetWifi() {
    if (!window.confirm('Forget stored WiFi credentials on the bridge?')) {
      return
    }

    const ok = await run('WiFi forget', () => backend.wifiForget(), rememberWifiStatus)
    if (ok) {
      wifiSsid = ''
      wifiPassword = ''
    }
  }

  function rememberWifiStatus(result: PsuWifiStatus) {
    wifi = result
    if (result.ssid) {
      wifiSsid = result.ssid
    }
  }

  async function refreshInfluxStatus(silent = false) {
    if (!connected || !backend.connected) {
      return false
    }

    if (silent) {
      try {
        rememberInfluxStatus(await backend.influxStatus())
        return true
      } catch {
        influx = null
        return false
      }
    }

    return run('Influx status', () => backend.influxStatus(), rememberInfluxStatus, 'read')
  }

  async function saveInflux(event: SubmitEvent) {
    event.preventDefault()
    const intervalMs = Number.parseInt(influxIntervalMs, 10)
    if (!Number.isFinite(intervalMs) || intervalMs < 80) {
      errorMessage = 'Influx interval must be at least 80 ms'
      return
    }
    if (influxEnabled && influxToken.length === 0 && !influx?.tokenSet) {
      errorMessage = 'Enter an InfluxDB API token before enabling reporting'
      return
    }

    const ok = await run(
      'Influx save',
      () => backend.configureInflux({
        enabled: influxEnabled,
        url: influxUrl.trim(),
        org: influxOrg.trim(),
        bucket: influxBucket.trim(),
        measurement: influxMeasurement.trim() || 'powerraid_psu',
        intervalMs,
        token: influxToken,
      }),
      rememberInfluxStatus,
    )
    if (ok) {
      influxToken = ''
    }
  }

  async function clearInfluxToken() {
    if (!window.confirm('Clear the stored InfluxDB API token and disable reporting?')) {
      return
    }

    const ok = await run(
      'Influx token',
      () => backend.configureInflux({
        enabled: false,
        url: influxUrl.trim(),
        org: influxOrg.trim(),
        bucket: influxBucket.trim(),
        measurement: influxMeasurement.trim() || 'powerraid_psu',
        intervalMs: Number.parseInt(influxIntervalMs, 10) || 80,
        clearToken: true,
      }),
      rememberInfluxStatus,
    )
    if (ok) {
      influxToken = ''
    }
  }

  function rememberInfluxStatus(result: PsuInfluxStatus) {
    influx = result
    influxEnabled = result.enabled
    influxUrl = result.url
    influxOrg = result.org || 'powerraid'
    influxBucket = result.bucket || 'zigbee'
    influxMeasurement = result.measurement || 'powerraid_psu'
    influxIntervalMs = String(result.intervalMs || 80)
  }

  function resetInfluxForm() {
    influxEnabled = false
    influxUrl = ''
    influxOrg = 'powerraid'
    influxBucket = 'zigbee'
    influxMeasurement = 'powerraid_psu'
    influxIntervalMs = '80'
    influxToken = ''
  }

  async function refreshAuthStatus(): Promise<PsuAuthStatus | null> {
    if (!backend.connected) {
      return null
    }
    try {
      auth = await backend.authStatus()
      return auth
    } catch {
      auth = null
      return null
    }
  }

  async function loginWithPassword(password: string) {
    passwordError = ''
    const ok = await run('Unlock', () => backend.login(password), (result) => {
      auth = result
    }, 'none')
    syncConnected()
    // A wrong password is expected input, not a transport failure.
    if (!ok) {
      passwordError = errorMessage
      errorMessage = ''
      return
    }
    passwordField = ''
    await afterLogin()
  }

  async function logout() {
    await run('Sign out', () => backend.logout(), (result) => {
      auth = result
    })
    await backend.stopMonitor().catch(() => undefined)
    monitor = null
    queueMonitorSync()
  }

  function passwordPolicyError(password: string): string {
    const min = auth?.passwordMinLength ?? 8
    const max = auth?.passwordMaxLength ?? 128
    if (password.length < min) return `Password must be at least ${min} characters`
    if (password.length > max) return `Password must be at most ${max} characters`
    return ''
  }

  async function submitPassword(event: SubmitEvent) {
    event.preventDefault()
    passwordError = ''
    const policyError = passwordPolicyError(passwordField)
    if (policyError) {
      passwordError = policyError
      return
    }
    if (passwordField !== confirmField) {
      passwordError = 'Passwords do not match'
      return
    }

    const ok = await run('Set password', () => backend.setPassword(passwordField), (result) => {
      auth = result
    })
    if (!ok) {
      passwordError = errorMessage
      errorMessage = ''
      return
    }
    passwordField = ''
    confirmField = ''
    await afterLogin()
  }

  async function saveFirstRunPassword(password: string) {
    passwordError = ''
    const policyError = passwordPolicyError(password)
    if (policyError) {
      passwordError = policyError
      return
    }

    const ok = await run('Set password', () => backend.setPassword(password), (result) => {
      auth = result
    })
    if (!ok) {
      passwordError = errorMessage
      errorMessage = ''
      return
    }
    markFirstRunPromptHandled()
    await afterLogin()
  }

  function dismissFirstRunPrompt() {
    markFirstRunPromptHandled()
  }

  function markFirstRunPromptHandled() {
    firstRunDismissed = true
    window.localStorage.setItem('pramon.passwordPromptShown', '1')
  }

  /** Loads everything that is only available once the bridge answers. */
  async function syncBridgeDetails() {
    const info = await backend.info().catch(() => null)
    if (info != null) {
      firmwareInfo = info
      lastAppliedCadence = null
      addressResult = {
        slot: selectedSlot,
        address: info.address,
        detected: info.detected,
        raw: info.raw,
        rawLines: info.rawLines,
      }
    }
    await refreshWifiStatus(true)
    await refreshInfluxStatus(true)
    await refreshPsuSlots(true)
    await refreshPowerPolicy(true)
    selectFirstAvailableSlot()
  }

  async function afterLogin() {
    // Writes the dashboard previously refused are now allowed, so refresh the
    // parts that need a session and restart the monitor with credentials.
    await refreshAuthStatus()
    await syncBridgeDetails()
    queueMonitorSync()
  }

  async function clearBridgePassword() {
    if (!window.confirm('Remove the bridge password? Anyone on the network will be able to change settings.')) {
      return
    }
    await run('Remove password', () => backend.clearPassword(), (result) => {
      auth = result
    })
    passwordField = ''
    confirmField = ''
    queueMonitorSync()
  }

  async function setTelemetryPublic(enabled: boolean) {
    await run('Telemetry access', () => backend.setTelemetryPublic(enabled), (result) => {
      auth = result
    })
    // The monitor stream is gated the same way, so restart it.
    queueMonitorSync()
  }

  /**
   * `access` says what the bridge must allow for the action to be attempted:
   * reading, writing, or nothing (connect and the auth calls themselves, which
   * exist precisely to establish or inspect that right).
   */
  async function run<T>(
    label: string,
    action: () => Promise<T>,
    onSuccess?: (result: T) => void,
    access: 'read' | 'write' | 'none' = 'write',
  ): Promise<boolean> {
    if (!backend.connected && access !== 'none') {
      errorMessage = 'Connect to the bridge first'
      return false
    }
    if ((access === 'read' && !canRead) || (access === 'write' && !canWrite)) {
      errorMessage = 'Unlock the bridge with its password first'
      return false
    }

    busy = label
    errorMessage = ''
    const startedAt = performance.now()
    try {
      const result = await action()
      onSuccess?.(result)
      syncConnected()
      lastCommandMs = performance.now() - startedAt
      return true
    } catch (error) {
      lastCommandMs = performance.now() - startedAt
      handleError(error)
      return false
    } finally {
      busy = ''
    }
  }

  function handleError(error: unknown) {
    syncConnected()
    errorMessage = error instanceof Error ? error.message : String(error)
  }

  function webApiEndpointLabel(value: string) {
    try {
      return normalizeWebApiBaseUrl(value)
    } catch {
      return '-'
    }
  }

  function syncConnected() {
    setConnected(backend.connected)
    authenticated = backend.connected && backend.authenticated
    canRead = backend.connected && backend.canRead
    canWrite = backend.connected && backend.canWrite
  }

  function setConnected(value: boolean) {
    if (connected !== value) {
      connected = value
    }
  }

  function updateRegisterWriteValue(register: string, event: Event) {
    registerWriteValues = {
      ...registerWriteValues,
      [register]: (event.currentTarget as HTMLInputElement).value,
    }
  }

  function handleFastCadenceChange(event: Event) {
    const checked = (event.currentTarget as HTMLInputElement).checked
    fastCadence = checked
    queueMonitorSync()
  }

  function stopMonitorSyncTimer() {
    if (monitorSyncTimer) {
      clearTimeout(monitorSyncTimer)
      monitorSyncTimer = null
    }
  }

  function queueMonitorSync(delayMs = 0) {
    stopMonitorSyncTimer()
    if (!connected) {
      return
    }
    monitorSyncTimer = setTimeout(() => {
      void syncMonitor()
    }, delayMs)
  }

  function queueMonitorRetry() {
    if (!connected || !backend.connected || !autoRefreshEnabled) {
      return
    }
    const delayMs = MONITOR_RETRY_DELAYS_MS[Math.min(monitorRetryAttempt, MONITOR_RETRY_DELAYS_MS.length - 1)]
    monitorRetryAttempt += 1
    const delaySeconds = Math.round(delayMs / 1_000)
    if (errorMessage) {
      errorMessage = `${errorMessage}; retrying in ${delaySeconds}s`
    }
    queueMonitorSync(delayMs)
  }

  async function syncMonitor() {
    monitorSyncTimer = null
    if (!connected || !backend.connected) {
      return
    }
    // A locked bridge cannot stream, and retrying would only spam 401s. The
    // monitor restarts from afterLogin() once a session exists.
    if (!canRead) {
      return
    }
    if (busy) {
      queueMonitorSync(150)
      return
    }

    const cadence = (fastCadence ? 'fast' : 'normal') as PsuCadenceMode
    if (lastAppliedCadence !== cadence) {
      const ok = await run('I2C cadence', () => backend.setCadence(cadence), undefined, 'read')
      if (!ok) {
        queueMonitorRetry()
        return
      }
      lastAppliedCadence = cadence
    }

    if (autoRefreshEnabled && monitorRegisterAddresses.length > 0) {
      const ok = await run(
        'Monitor',
        () => backend.startMonitor(safeAutoRefreshIntervalMs, monitorRegisterAddresses, 'all'),
        (result) => {
          monitor = result
        },
        'read',
      )
      if (ok) {
        monitorRetryAttempt = 0
      } else {
        queueMonitorRetry()
      }
    } else {
      monitorRetryAttempt = 0
      await run('Monitor stop', () => backend.stopMonitor(), (result) => {
        monitor = result
      }, 'read')
    }
  }

  function handleMonitorStreamError(error: Error) {
    handleError(error)
    queueMonitorRetry()
  }

  function handlePinsUpdate(result: PsuPins) {
    if (result.slot != null && result.slot !== selectedSlot) {
      slotPins = {
        ...slotPins,
        [result.slot]: result,
      }
      psuSlots = psuSlots.map((slot) =>
        slot.slot === result.slot
          ? {
              ...slot,
              ...result,
              slot: result.slot as number,
              name: slot.name,
              address: slot.address,
              detected: slot.detected,
              rawLine: result.rawLines?.[0] ?? slot.rawLine,
            }
          : slot,
      )
      rebuildStatusFromRegisters(
        slotStatuses[result.slot]?.address ?? psuSlots.find((slot) => slot.slot === result.slot)?.address ?? '?',
        result.rawLines,
        result.slot,
      )
      return
    }
    if (result.slot != null) {
      slotPins = {
        ...slotPins,
        [result.slot]: result,
      }
      psuSlots = psuSlots.map((slot) =>
        slot.slot === result.slot
          ? {
              ...slot,
              ...result,
              slot: result.slot as number,
              name: slot.name,
              address: slot.address,
              detected: slot.detected,
              rawLine: result.rawLines?.[0] ?? slot.rawLine,
            }
          : slot,
      )
    }
    pins = result
    rebuildStatusFromRegisters(status?.address ?? addressResult?.address ?? selectedSlotSummary?.address ?? firmwareInfo?.address ?? '?', result.rawLines, result.slot ?? selectedSlot)
  }

  function handleRegisterUpdate(update: PsuRegisterUpdate) {
    const key = registerKeyForAddress(update.registerAddress)
    if (key != null) {
      rememberSlotRegisterValue(
        update.slot ?? selectedSlot,
        key,
        update.value ?? update.result,
        update.result,
        update.cycle,
      )
    }

    if (!update.ok || update.value == null) {
      if (key != null) {
        if (update.slot == null || update.slot === selectedSlot) {
          readResult = {
            slot: update.slot ?? selectedSlot,
            address: update.address,
            register: update.register,
            value: update.result,
            raw: update.raw,
            rawLines: [update.rawLine],
          }
        }
      }
      return
    }

    if (update.slot == null || update.slot === selectedSlot) {
      readResult = {
        slot: update.slot ?? selectedSlot,
        address: update.address,
        register: update.register,
        value: update.value,
        raw: update.raw,
        rawLines: [update.rawLine],
      }
    }
    rebuildStatusFromRegisters(update.address, [update.rawLine], update.slot ?? selectedSlot)
  }

  function rememberSlotRegisterValue(
    slot: number,
    key: string,
    value: string,
    result: string,
    cycle: number | null,
  ) {
    const nextValues = {
      ...(slotRegisterValues[slot] ?? {}),
      [key]: value,
    }
    const nextEntries = {
      ...(slotRegisterTableEntries[slot] ?? {}),
      [key]: {
        value,
        result,
        updatedAt: Date.now(),
        cycle,
      },
    }
    slotRegisterValues = {
      ...slotRegisterValues,
      [slot]: nextValues,
    }
    slotRegisterTableEntries = {
      ...slotRegisterTableEntries,
      [slot]: nextEntries,
    }
    if (slot === selectedSlot) {
      registerValues = nextValues
      registerTableEntries = nextEntries
    }
  }

  function handleMonitorCycle(cycle: PsuMonitorCycle) {
    if (cycle.phase !== 'end') {
      return
    }
    void refreshPowerPolicy(true)
    const slots = cycle.slot === 'all'
      ? PSU_SLOT_IDS
      : [typeof cycle.slot === 'number' ? cycle.slot : selectedSlot]
    for (const slot of slots) {
      if (lastSampledCycleBySlot[slot] === cycle.cycle) {
        continue
      }
      lastSampledCycleBySlot = {
        ...lastSampledCycleBySlot,
        [slot]: cycle.cycle,
      }
      appendTelemetrySample(slot)
    }
  }

  function rebuildStatusFromRegisters(address: string, lines: string[] = [], slot: number | null = selectedSlot) {
    const slotId = slot ?? selectedSlot
    const values = slotRegisterValues[slotId] ?? (slotId === selectedSlot ? registerValues : {})
    const pinsForSlot = slotPins[slotId] ?? (slotId === selectedSlot ? pins : null)
    const next = statusFromRegisterValues(values, address, pinsForSlot, lines, slotId)
    if (next) {
      slotStatuses = {
        ...slotStatuses,
        [slotId]: next,
      }
      if (slotId === selectedSlot) {
        status = next
      }
    }
  }

  function appendTelemetrySample(slot: number) {
    const slotStatus = slotStatuses[slot]
    if (!slotStatus?.telemetry) {
      return
    }
    const sample: TelemetrySample = {
      timestamp: Date.now(),
      inputVoltageVrms: slotStatus.telemetry.inputVoltageVrms,
      inputPowerW: slotStatus.telemetry.inputPowerW,
      outputVoltageV: slotStatus.telemetry.outputVoltageV,
      outputCurrentA: slotStatus.telemetry.outputCurrentA,
      outputPowerW: slotStatus.telemetry.outputPowerW,
      efficiencyPercent: slotStatus.telemetry.efficiencyPercent,
      inletTemperatureC: slotStatus.telemetry.inletTemperatureC,
      internalTemperatureC: slotStatus.telemetry.internalTemperatureC,
      fan1SpeedRpm: slotStatus.telemetry.fan1SpeedRpm,
    }
    telemetryHistoryBySlot = {
      ...telemetryHistoryBySlot,
      [slot]: [...(telemetryHistoryBySlot[slot] ?? []).slice(-(MAX_TELEMETRY_HISTORY - 1)), sample],
    }
  }

  function stateWord(value: boolean, yes: string, no: string) {
    return value ? yes : no
  }

  function psuSlotTone(slot: PsuSlotSummary): Tone {
    if (!slot.present) {
      return 'muted'
    }
    if (slot.interruptAsserted || !slot.psokHigh) {
      return 'warn'
    }
    return 'good'
  }

  function psuSlotStatusLabel(slot: PsuSlotSummary) {
    if (!slot.present) {
      return 'Not present'
    }
    if (slot.psokHigh) {
      return slot.psonAsserted ? 'On · PSOK' : 'Standby · PSOK'
    }
    return slot.psonAsserted ? 'On · not OK' : 'Standby · not OK'
  }

  function psuSlotClass(slot: PsuSlotSummary) {
    const selected = slot.slot === selectedSlot
    const tone = psuSlotTone(slot)
    if (selected) {
      return 'border-primary bg-primary/10 text-foreground'
    }
    if (tone === 'warn') {
      return 'border-amber-500/45 bg-amber-500/10 text-amber-900 hover:bg-amber-500/15 dark:text-amber-100'
    }
    if (tone === 'good') {
      return 'border-emerald-500/45 bg-emerald-500/10 text-emerald-900 hover:bg-emerald-500/15 dark:text-emerald-100'
    }
    return 'border-border bg-card text-muted-foreground hover:bg-muted/50'
  }

  function wifiStatusDisplay(value: string) {
    switch (value) {
      case 'connected':
        return 'Connected'
      case 'disconnected':
        return 'Disconnected'
      case 'connect-failed':
        return 'Connect failed'
      case 'connection-lost':
        return 'Connection lost'
      case 'no-ssid':
        return 'SSID unavailable'
      case 'idle':
        return 'Idle'
      default:
        return value || 'Unknown'
    }
  }

  function toneFromBool(value: boolean): Tone {
    return value ? 'good' : 'bad'
  }

  function eventTone(value: string | undefined, activeTone: Tone): Tone {
    if (!value || value === 'n/a') {
      return 'muted'
    }
    return value === '0x0000' ? 'good' : activeTone
  }

  function temperatureTone(value: number, warningAt: number): Tone {
    if (!Number.isFinite(value)) {
      return 'muted'
    }
    if (value >= warningAt) {
      return 'warn'
    }
    return 'default'
  }

  function formatNumber(value: number, digits: number) {
    return Number.isFinite(value) ? value.toFixed(digits) : 'n/a'
  }

  function formatPowerWatts(value: number) {
    if (!Number.isFinite(value)) {
      return 'n/a'
    }
    if (Math.abs(value) >= 1000) {
      return `${(value / 1000).toFixed(2)} kW`
    }
    return `${Math.round(value)} W`
  }

  function formatEfficiency(value: number | null) {
    return value == null || !Number.isFinite(value) ? 'n/a' : `${value.toFixed(1)}%`
  }

  function formatTimestamp(timestamp: number) {
    const date = new Date(timestamp)
    return [date.getHours(), date.getMinutes(), date.getSeconds()]
      .map((value) => String(value).padStart(2, '0'))
      .join(':')
  }

  function registerAddress(register: RegisterOption) {
    return `0x${register.address.toString(16).toUpperCase().padStart(2, '0')}`
  }

  function hexWord(value: number) {
    return `0x${value.toString(16).toUpperCase().padStart(4, '0')}`
  }

  function registerWritePlaceholder(register: RegisterOption) {
    if (isResetOnlyRegister(register)) {
      return '0'
    }
    if (register.address === 0x3a) {
      return '0x0000'
    }
    if (FAN_RPM_REGISTER_ADDRESSES.has(register.address)) {
      return '9000'
    }
    if (register.address === 0x58) {
      return '12.10'
    }
    if (CURRENT_LIMIT_REGISTER_ADDRESSES.has(register.address)) {
      return '120'
    }
    return '0'
  }

  function registerWriteUnit(register: RegisterOption) {
    if (FAN_RPM_REGISTER_ADDRESSES.has(register.address)) {
      return 'RPM'
    }
    if (register.address === 0x58) {
      return 'V'
    }
    if (CURRENT_LIMIT_REGISTER_ADDRESSES.has(register.address)) {
      return 'A'
    }
    return ''
  }

  function parseRegisterWriteInput(register: RegisterOption, rawInput: string) {
    const input = rawInput.trim()
    if (!input) {
      errorMessage = `Enter a value for ${register.label}`
      return null
    }

    if (input.toLowerCase().startsWith('0x')) {
      const explicitRaw = parseRegisterWord(input)
      if (explicitRaw == null) {
        errorMessage = `${register.label} value must be a 16-bit hex word`
        return null
      }
      const validationError = registerWriteValidationError(register, explicitRaw)
      if (validationError) {
        errorMessage = validationError
        return null
      }

      return {
        raw: explicitRaw,
        display: parseRegisterDisplay(register, hexWord(explicitRaw)) || hexWord(explicitRaw),
      }
    }

    const value = parseHumanNumber(input)
    if (value == null) {
      errorMessage = `Could not parse ${register.label} value "${rawInput}"`
      return null
    }

    let raw = 0
    let display = ''
    if (isResetOnlyRegister(register)) {
      if (value !== 0) {
        errorMessage = `${register.label} only accepts 0 from this UI because nonzero writes are ignored by the PSU`
        return null
      }
      display = 'reset'
    } else if (register.address === 0x3a) {
      raw = integerWord(value, register.label)
      display = psControlSummary(hexWord(raw))
    } else if (FAN_RPM_REGISTER_ADDRESSES.has(register.address)) {
      if (value <= 0) {
        errorMessage = `${register.label} requires a nonzero RPM; clear fan ownership with the PS control action`
        return null
      }
      raw = integerWord(value, register.label)
      display = `${raw} RPM`
    } else if (register.address === 0x58) {
      if (value < 12 || value > 12.3) {
        errorMessage = 'Output voltage set must be 12.0 V through 12.3 V'
        return null
      }
      raw = scaledHumanWord(value, 1024, register.label)
      display = `${formatNumber(raw / 1024, 3)} V`
    } else if (CURRENT_LIMIT_REGISTER_ADDRESSES.has(register.address)) {
      if (value <= 0) {
        errorMessage = `${register.label} must be greater than 0 A`
        return null
      }
      raw = scaledHumanWord(value, 64, register.label)
      display = `${formatNumber(raw / 64, 2)} A`
    } else {
      raw = integerWord(value, register.label)
      display = String(raw)
    }

    const validationError = registerWriteValidationError(register, raw)
    if (validationError) {
      errorMessage = validationError
      return null
    }

    return { raw, display }
  }

  function parseHumanNumber(input: string): number | null {
    const normalized = input
      .trim()
      .toLowerCase()
      .replaceAll(',', '')
      .replace(/\s*(vrms|rpm|samples?|raw|bits?|[vaw])\s*$/, '')
      .trim()
    if (!/^-?\d+(?:\.\d+)?$/.test(normalized)) {
      return null
    }
    const value = Number(normalized)
    return Number.isFinite(value) ? value : null
  }

  function integerWord(value: number, label: string) {
    if (!Number.isInteger(value) || value < 0 || value > 0xffff) {
      throwRegisterInputError(`${label} must be an integer in 0..65535`)
    }
    return value
  }

  function scaledHumanWord(value: number, scale: number, label: string) {
    const raw = Math.round(value * scale)
    if (!Number.isInteger(raw) || raw < 0 || raw > 0xffff) {
      throwRegisterInputError(`${label} is outside the writable 16-bit range`)
    }
    return raw
  }

  function throwRegisterInputError(message: string): never {
    errorMessage = message
    throw new Error(message)
  }

  function parseRegisterDisplay(register: RegisterOption, raw: string) {
    const value = parseRegisterWord(raw)
    if (value == null) {
      return ''
    }

    switch (register.address) {
      case 0x00:
        return `FW ${(value >> 12) & 0x0f}.${(value >> 8) & 0x0f} · ${ucTypeName(String((value >> 4) & 0x0f))}`
      case 0x02:
        return psStatusSummary(value)
      case 0x04:
      case 0x06:
        return eventRegisterSummary(value)
      case 0x08:
        return `${formatNumber(scaledSignedWord(value, 32), 1)} Vrms`
      case 0x0a:
      case 0x10:
      case 0x34:
      case 0x36:
      case 0x5a:
      case 0x5c:
        return `${formatNumber(scaledSignedWord(value, 64), 2)} A`
      case 0x0c:
      case 0x12:
      case 0x32:
        return `${value} W`
      case 0x0e:
        return `${formatNumber(scaledSignedWord(value, 256), 3)} V`
      case 0x1a:
      case 0x1c:
        return `${formatNumber(scaledSignedWord(value, 64), 1)} C`
      case 0x1e:
      case 0x3c:
      case 0x3e:
        return `${value} RPM`
      case 0x30:
        return `${value} samples`
      case 0x3a:
        return psControlSummary(raw)
      case 0x58:
        return `${formatNumber(scaledSignedWord(value, 1024), 3)} V set`
      default:
        return ''
    }
  }

  function isResetOnlyRegister(register: RegisterOption) {
    return RESET_ONLY_REGISTER_ADDRESSES.has(register.address)
  }

  function parseRegisterWord(raw: string): number | null {
    const trimmed = raw.trim().toLowerCase()
    const isHex = trimmed.startsWith('0x')
    const isBareHexWord = /^[0-9a-f]{4}$/.test(trimmed)
    const digits = isHex ? trimmed.slice(2) : trimmed
    const valid = isHex || isBareHexWord ? /^[0-9a-f]{1,4}$/.test(digits) : /^[0-9]{1,5}$/.test(digits)
    if (!valid) {
      return null
    }
    const value = Number.parseInt(digits, isHex || isBareHexWord ? 16 : 10)
    return Number.isInteger(value) && value >= 0 && value <= 0xffff ? value : null
  }

  function scaledSignedWord(value: number, divisor: number) {
    const signed = value & 0x8000 ? value - 0x10000 : value
    return signed / divisor
  }

  function psStatusSummary(value: number) {
    const parts = [
      (value & (1 << 1)) !== 0 ? 'in ok' : 'in fault',
      (value & (1 << 0)) !== 0 ? 'out ok' : 'out off',
      (value & (1 << 2)) !== 0 ? 'PSON on' : 'PSON off',
      vendorName(String((value >> 3) & 0x07)),
      inputRangeSummary((value >> 8) & 0x07),
    ]
    return parts.join(' · ')
  }

  function eventRegisterSummary(value: number) {
    if (value === 0) {
      return 'clear'
    }
    return `bits ${activeBits(value).join(',')}`
  }

  function activeBits(value: number) {
    const bits: number[] = []
    for (let bit = 0; bit < 16; bit += 1) {
      if ((value & (1 << bit)) !== 0) {
        bits.push(bit)
      }
    }
    return bits
  }

  function inputRangeSummary(value: number) {
    const names: Record<number, string> = {
      0: 'out-of-range',
      1: 'low-line',
      2: 'mid-line',
      3: 'high-line',
      4: '48VDC',
      5: '420VDC',
    }
    return names[value] ?? 'reserved'
  }

  function psControlActionClearOnly(action: { bit: number; clearOnly?: boolean }) {
    return 'clearOnly' in action && action.clearOnly === true
  }

  function psControlActionCaution(action: { bit: number; caution?: boolean }) {
    return 'caution' in action && action.caution === true
  }

  function psControlActionClass(action: { bit: number; clearOnly?: boolean; caution?: boolean }, active: boolean) {
    if (psControlActionClearOnly(action) && !active) {
      return ''
    }
    if (psControlActionCaution(action)) {
      return active
        ? 'border-amber-500/70 bg-amber-500/20 text-amber-900 hover:bg-amber-500/25 dark:text-amber-100'
        : 'border-amber-500/45 bg-amber-500/10 text-amber-800 hover:bg-amber-500/20 dark:text-amber-200'
    }
    return active
      ? 'border-primary/60 bg-primary/15 text-primary hover:bg-primary/20'
      : 'border-sky-500/45 bg-sky-500/10 text-sky-800 hover:bg-sky-500/20 dark:text-sky-200'
  }

  async function applyPsControlAction(action: PsControlActionRow) {
    if (psControlWord == null) {
      errorMessage = 'Read or listen to PS control 0x3A before changing PS control bits'
      return
    }
    const mask = 1 << action.bit
    const active = (psControlWord & mask) !== 0
    const next = psControlActionClearOnly(action) || active
      ? psControlWord & ~mask
      : psControlWord | mask
    if (next === psControlWord) {
      return
    }
    const verb = psControlActionClearOnly(action) ? 'Clear' : active ? 'Disable' : 'Enable'
    await writePsControlWord(`${verb} ${action.label}`, next, psControlActionCaution(action))
  }

  function psControlSystemTypeClass(systemType: { caution: boolean }, active: boolean) {
    if (systemType.caution) {
      return active
        ? 'border-amber-500/70 bg-amber-500/20 text-amber-900 hover:bg-amber-500/25 dark:text-amber-100'
        : 'border-amber-500/45 bg-amber-500/10 text-amber-800 hover:bg-amber-500/20 dark:text-amber-200'
    }
    return active
      ? 'border-primary/60 bg-primary/15 text-primary hover:bg-primary/20'
      : 'border-sky-500/45 bg-sky-500/10 text-sky-800 hover:bg-sky-500/20 dark:text-sky-200'
  }

  async function setPsControlSystemType(systemType: PsControlSystemTypeRow) {
    if (psControlWord == null) {
      errorMessage = 'Read or listen to PS control 0x3A before changing PS control system type'
      return
    }
    const next = (psControlWord & ~0x0007) | systemType.value
    if (next === psControlWord) {
      return
    }
    await writePsControlWord(`Set PS control system type ${systemType.label}`, next, systemType.caution)
  }

  async function writePsControlWord(label: string, value: number, caution: boolean) {
    const psControlRegister = REGISTER_TABLE.find((register) => register.address === 0x3a)
    const validationError = psControlRegister ? registerWriteValidationError(psControlRegister, value) : ''
    if (validationError) {
      errorMessage = validationError
      return
    }

    const raw = hexWord(value)
    const cautionText = caution
      ? ' This can alter voltage mode, fan-fault behavior, standby behavior, or PSU interrupts.'
      : ''
    if (!window.confirm(`${label}: write PS control 0x3A = ${raw}?${cautionText}`)) {
      return
    }
    const ok = await run(label, () => backend.writeRegister('ps-control', value, selectedSlot), (result) => {
      readResult = result
      rememberSlotRegisterValue(result.slot ?? selectedSlot, 'ps-control', result.value, 'ok', null)
      rebuildStatusFromRegisters(result.address, result.rawLines, result.slot ?? selectedSlot)
    })
    if (ok && connected) {
      queueMonitorSync()
    }
  }

  function toggleRegisterListen(register: RegisterOption) {
    listenedRegisters = {
      ...listenedRegisters,
      [register.value]: !(listenedRegisters[register.value] ?? false),
    }
    queueMonitorSync()
  }

  function vendorName(value: string) {
    return value === '2' ? 'LiteOn' : `vendor ${value}`
  }

  function formatBridgeFirmwareLabel(info: PsuFirmwareInfo) {
    const version = info.firmware
      .trim()
      .replace(/^bridge(?:\s+firmware)?\s*/i, '')
      .trim()
    return `${version && version !== '?' ? version : 'unknown'} · p${info.protocol}`
  }

  function ucTypeName(value: string) {
    const names: Record<string, string> = {
      '0': 'Microchip uC',
      '1': 'ST uC',
      '2': 'Freescale uC',
      '3': 'TI uC',
      '4': 'Silicon Labs uC',
    }
    return names[value] ?? `uC type ${value}`
  }

  function psControlSummary(raw: string) {
    const value = parseRegisterWord(raw)
    if (value == null) {
      return raw
    }
    const systemType = value & 0x0007
    const interrupt = (value & (1 << 5)) !== 0
    const fan1 = (value & (1 << 6)) !== 0
    const fan2 = (value & (1 << 7)) !== 0
    const fan = fan1 || fan2 ? 'fan host' : 'fan auto'
    return `sys${systemType} ${interrupt ? 'int set' : 'int clear'} ${fan}`
  }

  function timeAgo(timestamp: number) {
    const elapsedSeconds = Math.max(0, Math.round((Date.now() - timestamp) / 1000))
    if (elapsedSeconds < 2) {
      return 'now'
    }
    if (elapsedSeconds < 60) {
      return `${elapsedSeconds}s ago`
    }
    return `${Math.floor(elapsedSeconds / 60)}m ago`
  }

  function expandTelemetry(key: TelemetrySeriesKey) {
    if (allTelemetrySampleCount > 0) {
      expandedTelemetryKey = key
    }
  }

  function closeExpandedTelemetry() {
    expandedTelemetryKey = null
  }

  function buildTelemetryMetrics(history: Record<number, TelemetrySample[]>, slot: number): TelemetryMetricRow[] {
    return [
      {
        key: 'inputVoltageVrms',
        label: 'Vin',
        value: summaryTelemetryValue('inputVoltageVrms', slot, history),
        tone: summaryTelemetryTone('inputVoltageVrms', slot, history),
      },
      {
        key: 'inputPowerW',
        label: 'Pin',
        value: summaryTelemetryValue('inputPowerW', slot, history),
        tone: summaryTelemetryTone('inputPowerW', slot, history),
      },
      {
        key: 'outputVoltageV',
        label: 'Vout',
        value: summaryTelemetryValue('outputVoltageV', slot, history),
        tone: summaryTelemetryTone('outputVoltageV', slot, history),
      },
      {
        key: 'outputCurrentA',
        label: 'Iout',
        value: summaryTelemetryValue('outputCurrentA', slot, history),
        tone: summaryTelemetryTone('outputCurrentA', slot, history),
      },
      {
        key: 'outputPowerW',
        label: 'Pout',
        value: summaryTelemetryValue('outputPowerW', slot, history),
        tone: summaryTelemetryTone('outputPowerW', slot, history),
      },
      {
        key: 'efficiencyPercent',
        label: 'Efficiency',
        value: summaryTelemetryValue('efficiencyPercent', slot, history),
        tone: summaryTelemetryTone('efficiencyPercent', slot, history),
      },
      {
        key: 'inletTemperatureC',
        label: 'Inlet temp',
        value: summaryTelemetryValue('inletTemperatureC', slot, history),
        tone: summaryTelemetryTone('inletTemperatureC', slot, history),
      },
      {
        key: 'internalTemperatureC',
        label: 'Internal temp',
        value: summaryTelemetryValue('internalTemperatureC', slot, history),
        tone: summaryTelemetryTone('internalTemperatureC', slot, history),
      },
      {
        key: 'fan1SpeedRpm',
        label: 'Fan 1',
        value: summaryTelemetryValue('fan1SpeedRpm', slot, history),
        tone: summaryTelemetryTone('fan1SpeedRpm', slot, history),
      },
    ]
  }

  function telemetryUnit(key: TelemetrySeriesKey) {
    const units: Record<TelemetrySeriesKey, string> = {
      inputVoltageVrms: 'Vrms',
      inputPowerW: 'W',
      outputVoltageV: 'V',
      outputCurrentA: 'A',
      outputPowerW: 'W',
      efficiencyPercent: '%',
      inletTemperatureC: 'C',
      internalTemperatureC: 'C',
      fan1SpeedRpm: 'RPM',
    }
    return units[key]
  }

  function telemetryDigits(key: TelemetrySeriesKey) {
    const digits: Record<TelemetrySeriesKey, number> = {
      inputVoltageVrms: 1,
      inputPowerW: 0,
      outputVoltageV: 3,
      outputCurrentA: 2,
      outputPowerW: 0,
      efficiencyPercent: 1,
      inletTemperatureC: 1,
      internalTemperatureC: 1,
      fan1SpeedRpm: 0,
    }
    return digits[key]
  }

  function formatTelemetryValue(key: TelemetrySeriesKey, value: number) {
    return `${formatNumber(value, telemetryDigits(key))} ${telemetryUnit(key)}`
  }

  function latestTelemetryTimestamp(summary: TelemetryHistorySummary[] = telemetryHistorySummary) {
    const timestamps = summary
      .map((slot) => slot.latestTimestamp)
      .filter((value): value is number => typeof value === 'number')
    return timestamps.length > 0 ? Math.max(...timestamps) : null
  }

  function latestTelemetrySample(slot: number, history: Record<number, TelemetrySample[]> = telemetryHistoryBySlot) {
    return history[slot]?.at(-1) ?? null
  }

  function latestTelemetrySampleAcrossSlots(
    key: TelemetrySeriesKey,
    history: Record<number, TelemetrySample[]> = telemetryHistoryBySlot,
  ) {
    let latest: TelemetrySample | null = null
    for (const slot of PSU_SLOT_IDS) {
      const sample = latestTelemetrySample(slot, history)
      if (sample == null) {
        continue
      }
      const value = sample?.[key]
      if (typeof value !== 'number' || !Number.isFinite(value)) {
        continue
      }
      if (latest == null || sample.timestamp > latest.timestamp) {
        latest = sample
      }
    }
    return latest
  }

  function summaryTelemetrySample(
    key: TelemetrySeriesKey,
    slot = selectedSlot,
    history: Record<number, TelemetrySample[]> = telemetryHistoryBySlot,
  ) {
    const selectedSample = latestTelemetrySample(slot, history)
    const selectedValue = selectedSample?.[key]
    if (typeof selectedValue === 'number' && Number.isFinite(selectedValue)) {
      return selectedSample
    }
    return latestTelemetrySampleAcrossSlots(key, history)
  }

  function latestTelemetryValue(
    key: TelemetrySeriesKey,
    slot = selectedSlot,
    history: Record<number, TelemetrySample[]> = telemetryHistoryBySlot,
  ) {
    const sample = latestTelemetrySample(slot, history)
    const value = sample?.[key]
    return typeof value === 'number' && Number.isFinite(value) ? formatTelemetryValue(key, value) : 'n/a'
  }

  function summaryTelemetryValue(
    key: TelemetrySeriesKey,
    slot = selectedSlot,
    history: Record<number, TelemetrySample[]> = telemetryHistoryBySlot,
  ) {
    const sample = summaryTelemetrySample(key, slot, history)
    const value = sample?.[key]
    return typeof value === 'number' && Number.isFinite(value) ? formatTelemetryValue(key, value) : 'n/a'
  }

  function telemetryMetricTone(
    key: TelemetrySeriesKey,
    slot = selectedSlot,
    history: Record<number, TelemetrySample[]> = telemetryHistoryBySlot,
  ): Tone {
    const sample = latestTelemetrySample(slot, history)
    const value = sample?.[key]
    if (typeof value !== 'number' || !Number.isFinite(value)) {
      return 'muted'
    }
    if (key === 'inletTemperatureC') {
      return temperatureTone(value, 50)
    }
    if (key === 'internalTemperatureC') {
      return temperatureTone(value, 90)
    }
    return 'default'
  }

  function summaryTelemetryTone(
    key: TelemetrySeriesKey,
    slot = selectedSlot,
    history: Record<number, TelemetrySample[]> = telemetryHistoryBySlot,
  ): Tone {
    const selectedSample = latestTelemetrySample(slot, history)
    const selectedValue = selectedSample?.[key]
    if (typeof selectedValue === 'number' && Number.isFinite(selectedValue)) {
      return telemetryMetricTone(key, slot, history)
    }
    const sample = latestTelemetrySampleAcrossSlots(key, history)
    const value = sample?.[key]
    if (typeof value !== 'number' || !Number.isFinite(value)) {
      return 'muted'
    }
    if (key === 'inletTemperatureC') {
      return temperatureTone(value, 50)
    }
    if (key === 'internalTemperatureC') {
      return temperatureTone(value, 90)
    }
    return 'default'
  }

  function telemetrySlotLabel(slot: number) {
    return psuSlots.find((candidate) => candidate.slot === slot)?.name?.replace(/^PSU/, 'PSU ') ?? `PSU ${slot}`
  }

  function visibleTelemetrySeries(key: TelemetrySeriesKey, slot: number): TelemetryPoint[] {
    const latestTimestamp = latestTelemetryTimestamp()
    if (latestTimestamp == null) {
      return []
    }
    const windowStart = latestTimestamp - SPARKLINE_WINDOW_MS
    const samples = (telemetryHistoryBySlot[slot] ?? [])
      .map((sample) => ({ timestamp: sample.timestamp, value: sample[key] }))
      .filter(
        (sample): sample is { timestamp: number; value: number } =>
          typeof sample.value === 'number' &&
          Number.isFinite(sample.value),
      )

    const visible = samples.filter((sample) => sample.timestamp >= windowStart)
    const previous = [...samples].reverse().find((sample) => sample.timestamp < windowStart)
    if (previous && visible.length > 0) {
      const next = visible[0]
      const span = next.timestamp - previous.timestamp
      if (span > 0) {
        const ratio = (windowStart - previous.timestamp) / span
        visible.unshift({
          timestamp: windowStart,
          value: previous.value + (next.value - previous.value) * ratio,
        })
      }
    }

    return visible
  }

  function telemetryValueExtent(key: TelemetrySeriesKey): [number, number] {
    const values = PSU_SLOT_IDS
      .flatMap((slot) => visibleTelemetrySeries(key, slot).map((sample) => sample.value))
      .filter((value) => Number.isFinite(value))
    if (values.length === 0) {
      return [0, 1]
    }
    const min = Math.min(...values)
    const max = Math.max(...values)
    return [min, max]
  }

  function chartPoints(
    key: TelemetrySeriesKey,
    slot: number,
    width: number,
    height: number,
    paddingX = 0,
    paddingY = 0,
  ): ChartPoint[] {
    const latestTimestamp = latestTelemetryTimestamp()
    if (latestTimestamp == null) {
      return []
    }
    const windowStart = latestTimestamp - SPARKLINE_WINDOW_MS
    const samples = visibleTelemetrySeries(key, slot)
    if (samples.length === 0) {
      return []
    }
    const [min, max] = telemetryValueExtent(key)
    const span = max - min || Math.max(Math.abs(max), 1)
    const plotWidth = width - paddingX * 2
    const plotHeight = height - paddingY * 2
    return samples.map((sample) => {
      const x = paddingX + ((sample.timestamp - windowStart) / SPARKLINE_WINDOW_MS) * plotWidth
      const y = paddingY + plotHeight - ((sample.value - min) / span) * plotHeight
      return {
        x: Math.max(paddingX, Math.min(width - paddingX, x)),
        y,
        timestamp: sample.timestamp,
        value: sample.value,
      }
    })
  }

  function smoothPath(points: Pick<ChartPoint, 'x' | 'y'>[]) {
    if (points.length < 2) {
      return ''
    }
    if (points.length === 2) {
      const [first, second] = points
      return `M ${first.x.toFixed(2)} ${first.y.toFixed(2)} L ${second.x.toFixed(2)} ${second.y.toFixed(2)}`
    }

    let path = `M ${points[0].x.toFixed(2)} ${points[0].y.toFixed(2)}`
    for (let index = 0; index < points.length - 1; index += 1) {
      const p0 = points[Math.max(0, index - 1)]
      const p1 = points[index]
      const p2 = points[index + 1]
      const p3 = points[Math.min(points.length - 1, index + 2)]
      const cp1x = p1.x + (p2.x - p0.x) / 6
      const cp1y = p1.y + (p2.y - p0.y) / 6
      const cp2x = p2.x - (p3.x - p1.x) / 6
      const cp2y = p2.y - (p3.y - p1.y) / 6
      path += ` C ${cp1x.toFixed(2)} ${cp1y.toFixed(2)}, ${cp2x.toFixed(2)} ${cp2y.toFixed(2)}, ${p2.x.toFixed(2)} ${p2.y.toFixed(2)}`
    }
    return path
  }

  function telemetrySeriesPaths(
    key: TelemetrySeriesKey,
    width: number,
    height: number,
    paddingX: number,
    paddingY: number,
    baselineY: number,
  ): TelemetrySeriesPath[] {
    const series: TelemetrySeriesPath[] = []
    for (const slot of PSU_SLOT_IDS) {
      const points = chartPoints(key, slot, width, height, paddingX, paddingY)
      const path = smoothPath(points)
      if (!path || points.length < 2) {
        continue
      }
      const first = points[0]
      const last = points[points.length - 1]
      series.push({
        slot,
        label: telemetrySlotLabel(slot),
        color: TELEMETRY_SLOT_COLORS[slot],
        path,
        area: `${path} L ${last.x.toFixed(2)} ${baselineY} L ${first.x.toFixed(2)} ${baselineY} Z`,
        latest: latestTelemetryValue(key, slot),
        sampleCount: telemetryHistoryBySlot[slot]?.length ?? 0,
      })
    }
    return series
  }

  function sparklineSeries(key: TelemetrySeriesKey) {
    return telemetrySeriesPaths(key, 100, 40, 0, 0, 40)
  }

  function detailedChartSeries(key: TelemetrySeriesKey) {
    return telemetrySeriesPaths(key, 640, 320, 56, 32, 288)
  }

  function detailedChartYTicks(key: TelemetrySeriesKey) {
    const visibleValues = PSU_SLOT_IDS.flatMap((slot) => visibleTelemetrySeries(key, slot).map((sample) => sample.value))
    if (visibleValues.length === 0) {
      return []
    }
    const min = Math.min(...visibleValues)
    const max = Math.max(...visibleValues)
    const span = max - min || Math.max(Math.abs(max), 1)
    return [0, 0.25, 0.5, 0.75, 1].map((ratio) => {
      const value = min + span * ratio
      return {
        value,
        y: 288 - ratio * 256,
        label: formatNumber(value, telemetryDigits(key)),
      }
    }).reverse()
  }

</script>

<main class="min-h-svh bg-background">
  <div class="mx-auto flex w-full max-w-[1440px] flex-col gap-4 px-3 py-4 sm:px-5 lg:px-6 lg:py-6">
    <AppHeader
      {connected}
      {connectionState}
      {connectionLabel}
      {connectionTone}
    />

    {#if connected && auth?.configured && !authenticated}
      <section class="rounded-lg border border-amber-500/45 bg-amber-500/10 px-4 py-3 text-sm text-amber-900 dark:text-amber-100" role="status">
        <div class="flex flex-wrap items-center justify-between gap-3">
          <span>
            This bridge is password protected.
            {auth.telemetryPublic ? 'Readings stay visible; settings need the password.' : 'Unlock to view and change anything.'}
          </span>
          <span class="font-mono text-xs">{auth.lockoutMs > 0 ? `Locked out for ${Math.ceil(auth.lockoutMs / 1000)}s` : ''}</span>
        </div>
      </section>
    {/if}
    <section class="grid gap-3 md:grid-cols-3">
      {#each psuSlots as slot}
        <button
          type="button"
          class={`flex min-h-28 flex-col justify-between rounded-lg border p-4 text-left transition ${psuSlotClass(slot)}`}
          onclick={() => selectPsuSlot(slot.slot)}
          disabled={!connected || busy !== ''}
          aria-pressed={slot.slot === selectedSlot}
        >
          <div class="flex items-start justify-between gap-3">
            <div>
              <div class="text-sm font-semibold text-foreground">PSU {slot.slot}</div>
              <div class="mt-1 text-xs">{slot.address}</div>
            </div>
            <div
              class="mt-1 size-2.5 rounded-full"
              class:bg-emerald-500={psuSlotTone(slot) === 'good'}
              class:bg-amber-500={psuSlotTone(slot) === 'warn'}
              class:bg-muted-foreground={psuSlotTone(slot) === 'muted'}
            ></div>
          </div>
          <div class="mt-4 grid grid-cols-2 gap-2 text-xs">
            <div>
              <div class="text-muted-foreground">State</div>
              <div class="font-medium text-foreground">{psuSlotStatusLabel(slot)}</div>
            </div>
            <div>
              <div class="text-muted-foreground">Bridge</div>
              <div class="font-medium text-foreground">{slot.detected ? 'Detected' : 'Not detected'}</div>
            </div>
          </div>
        </button>
      {/each}
    </section>

    <section class="flex flex-wrap items-center justify-between gap-3 rounded-lg border bg-card px-4 py-3">
      <div>
        <div class="text-sm font-semibold">{selectedSlotLabel}</div>
        <div class="text-xs text-muted-foreground">{selectedSlotStatus} · {selectedAddress}</div>
      </div>
      <button
        type="button"
        class="rounded-md border px-3 py-2 text-sm transition hover:bg-muted disabled:cursor-not-allowed disabled:opacity-50"
        onclick={() => refreshPsuSlots(false)}
        disabled={!canRead || busy !== ''}
      >
        Refresh slots
      </button>
    </section>

    <section class="grid gap-4 md:grid-cols-2 xl:grid-cols-[340px_340px_1fr]">
      <ConnectionCard
        bind:webApiBaseUrl
        bind:autoRefreshEnabled
        bind:autoRefreshMs
        bind:fastCadence
        {connected}
        {busy}
        {connectionDescription}
        {safeAutoRefreshIntervalMs}
        {connect}
        {disconnect}
        {handleFastCadenceChange}
        {formatNumber}
      />

      <WifiCard
        bind:wifiSsid
        bind:wifiPassword
        bind:showWifiPassword
        {connected}
        {canWrite}
        {busy}
        {wifi}
        {wifiStatusLabel}
        {wifiStatusTone}
        refreshWifi={async () => { await refreshWifiStatus(false) }}
        {connectWifi}
        {reconnectWifi}
        {disconnectWifi}
        {forgetWifi}
      />

      <SecurityCard
        bind:showPassword
        bind:passwordField
        bind:confirmField
        {connected}
        {busy}
        {auth}
        {canWrite}
        {authenticated}
        {submitPassword}
        login={loginWithPassword}
        {logout}
        clearPassword={clearBridgePassword}
        {setTelemetryPublic}
        {passwordError}
      />

      <div class="grid gap-4 md:col-span-2 xl:col-span-1">
        <BridgeCard
          {busy}
          {connected}
          {bridgeFirmwareLabel}
          {monitor}
          transportLabel={transportName}
          {transportDetailLabel}
          {transportDetail}
          {firmwareInfo}
        />
        <ActionsCard
          {busy}
          {connected}
          {canWrite}
          {selectedSlotLabel}
          {selectedSlotStatus}
          {selectedAddress}
          {psonDriveAsserted}
          {psonToggleLabel}
          {togglePson}
          {clearInterrupt}
        />
      </div>
    </section>

    <section class="grid gap-4 md:grid-cols-2">
      <PowerPolicyCard
        {busy}
        {connected}
        {canWrite}
        {powerPolicy}
        description={powerPolicyDescription}
        {setPowerPolicyOff}
        {setPowerPolicyAutomatic}
        {setPowerPolicyRedundancy}
        {setPowerPolicyEfficiencyProfile}
        {formatPowerWatts}
      />

      <InfluxCard
        bind:influxEnabled
        bind:influxUrl
        bind:influxOrg
        bind:influxBucket
        bind:influxMeasurement
        bind:influxIntervalMs
        bind:influxToken
        {connected}
        {canWrite}
        {busy}
        {influx}
        refreshInflux={async () => { await refreshInfluxStatus(false) }}
        {saveInflux}
        {clearInfluxToken}
      />
    </section>

    <section class="grid gap-4 xl:grid-cols-[0.9fr_1.4fr]">
      <div class="grid gap-4">
        <StatusPinsCard
          subtitle={pins ? statusSubtitle : 'No status sample'}
          {statusMetrics}
          {pinRows}
        />
      </div>

      <TelemetryCard
        subtitle={telemetrySubtitle}
        {telemetryMetrics}
        hasTelemetryHistory={allTelemetrySampleCount > 0}
        {sparklineSeries}
        {expandTelemetry}
      />
    </section>

    <section class="grid gap-4">
      <EventsCard
        {eventRows}
        {connected}
        {canWrite}
        {busy}
        {psControlWord}
        {psControlActionRows}
        {psControlSystemTypeRows}
        {clearInterrupt}
        {hexWord}
        {applyPsControlAction}
        {setPsControlSystemType}
      />
    </section>

    <section class="grid gap-4">
      <RegistersCard
        {registerRows}
        {listenedRegisterCount}
        {readResult}
        {selectedAddress}
        {registerWriteValues}
        {connected}
        {canRead}
        {canWrite}
        {busy}
        {registerAddress}
        {toggleRegisterListen}
        {readRegisterValue}
        {isResetOnlyRegister}
        {resetRegisterValue}
        {writeRegisterValue}
        {updateRegisterWriteValue}
        {registerWritePlaceholder}
        {registerWriteUnit}
      />
    </section>
  </div>
</main>

{#if showFirstRunPrompt}
  <FirstRunPasswordDialog
    minLength={auth?.passwordMinLength ?? 8}
    {busy}
    {passwordError}
    save={saveFirstRunPassword}
    dismiss={dismissFirstRunPrompt}
  />
{/if}

{#if expandedTelemetryMetric}
  <TelemetryDialog
    metric={expandedTelemetryMetric}
    telemetryHistoryLength={allTelemetrySampleCount}
    latestTelemetryValue={summaryTelemetryValue}
    {detailedChartYTicks}
    {detailedChartSeries}
    {telemetryUnit}
    close={closeExpandedTelemetry}
  />
{/if}
