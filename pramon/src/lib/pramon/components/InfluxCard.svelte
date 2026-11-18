<script lang="ts">
  import DatabaseIcon from '@lucide/svelte/icons/database'
  import KeyRoundIcon from '@lucide/svelte/icons/key-round'
  import RefreshCwIcon from '@lucide/svelte/icons/refresh-cw'
  import SaveIcon from '@lucide/svelte/icons/save'
  import Trash2Icon from '@lucide/svelte/icons/trash-2'
  import { Button } from '$lib/components/ui/button/index.js'
  import { Input } from '$lib/components/ui/input/index.js'
  import { Label } from '$lib/components/ui/label/index.js'
  import type { PsuInfluxStatus } from '$lib/psu'
  import CompactStack from './CompactStack.svelte'
  import DashboardCard from './DashboardCard.svelte'
  import type { Tone } from '../types'

  let {
    connected,
    canWrite,
    busy,
    influx,
    influxEnabled = $bindable<boolean>(),
    influxUrl = $bindable<string>(),
    influxOrg = $bindable<string>(),
    influxBucket = $bindable<string>(),
    influxMeasurement = $bindable<string>(),
    influxIntervalMs = $bindable<string>(),
    influxToken = $bindable<string>(),
    refreshInflux,
    saveInflux,
    clearInfluxToken,
  }: {
    connected: boolean
    canWrite: boolean
    busy: string
    influx: PsuInfluxStatus | null
    influxEnabled: boolean
    influxUrl: string
    influxOrg: string
    influxBucket: string
    influxMeasurement: string
    influxIntervalMs: string
    influxToken: string
    refreshInflux: () => void | Promise<void>
    saveInflux: (event: SubmitEvent) => void | Promise<void>
    clearInfluxToken: () => void | Promise<void>
  } = $props()

  let influxBusy = $derived(busy.startsWith('Influx'))
  let actionDisabled = $derived(!canWrite || busy !== '')
  let saveDisabled = $derived(actionDisabled || (influxEnabled && (influxUrl.length === 0 || influxOrg.length === 0 || influxBucket.length === 0)))
  let statusLabel = $derived(!connected
    ? 'Bridge disconnected'
    : influx == null
      ? 'Status unknown'
      : influx.enabled
        ? influx.configured
          ? influx.lastResult === 'ok'
            ? 'Writing'
            : 'Enabled'
          : 'Incomplete'
        : 'Disabled')
  let statusTone = $derived((!connected || influx == null
    ? 'muted'
    : influx.enabled && influx.configured && influx.lastResult === 'ok'
      ? 'good'
      : influx.enabled && (!influx.configured || influx.lastError)
        ? 'warn'
        : influx.enabled
          ? 'default'
          : 'muted') as Tone)
  let tokenLabel = $derived(influx?.tokenSet ? 'Set' : 'Missing')
  let tokenTone = $derived((influx?.tokenSet ? 'good' : 'muted') as Tone)
  let writesLabel = $derived(influx == null ? '-' : String(influx.writeCount))
  let httpLabel = $derived(influx == null || influx.lastHttpStatus === 0 ? '-' : String(influx.lastHttpStatus))
</script>

<DashboardCard title="InfluxDB" description={statusLabel} icon={DatabaseIcon}>
  <div class="grid overflow-hidden rounded-[8px] border text-sm sm:grid-cols-2" aria-live="polite">
    <CompactStack label="State" value={statusLabel} tone={statusTone} />
    <CompactStack label="Token" value={tokenLabel} tone={tokenTone} />
    <CompactStack label="Writes" value={writesLabel} tone={influx?.writeCount ? 'default' : 'muted'} />
    <CompactStack label="HTTP" value={httpLabel} tone={influx?.lastHttpStatus && influx.lastHttpStatus >= 300 ? 'warn' : 'muted'} />
  </div>

  <form class="grid gap-3 border-t pt-4" onsubmit={saveInflux}>
    <label class="flex items-center gap-2 text-sm font-medium">
      <input
        type="checkbox"
        class="size-4 accent-primary"
        bind:checked={influxEnabled}
        disabled={actionDisabled}
      />
      Enable reporting
    </label>

    <div class="grid gap-2">
      <Label for="influx-url">URL</Label>
      <Input
        id="influx-url"
        type="url"
        bind:value={influxUrl}
        placeholder="http://192.168.1.10:8086"
        autocomplete="off"
        disabled={actionDisabled}
      />
    </div>

    <div class="grid gap-3 sm:grid-cols-2">
      <div class="grid gap-2">
        <Label for="influx-org">Org</Label>
        <Input id="influx-org" bind:value={influxOrg} autocomplete="off" disabled={actionDisabled} />
      </div>
      <div class="grid gap-2">
        <Label for="influx-bucket">Bucket</Label>
        <Input id="influx-bucket" bind:value={influxBucket} autocomplete="off" disabled={actionDisabled} />
      </div>
    </div>

    <div class="grid gap-3 sm:grid-cols-2">
      <div class="grid gap-2">
        <Label for="influx-measurement">Measurement</Label>
        <Input id="influx-measurement" bind:value={influxMeasurement} autocomplete="off" disabled={actionDisabled} />
      </div>
      <div class="grid gap-2">
        <Label for="influx-interval">Interval ms</Label>
        <Input id="influx-interval" type="number" min="80" step="80" bind:value={influxIntervalMs} disabled={actionDisabled} />
      </div>
    </div>

    <div class="grid gap-2">
      <Label for="influx-token">API token</Label>
      <div class="flex gap-2">
        <Input
          id="influx-token"
          type="password"
          bind:value={influxToken}
          placeholder={influx?.tokenSet ? 'Existing token retained' : 'InfluxDB API token'}
          autocomplete="off"
          disabled={actionDisabled}
        />
        <Button
          type="button"
          variant="outline"
          size="icon"
          onclick={clearInfluxToken}
          disabled={actionDisabled || !influx?.tokenSet}
          aria-label="Clear InfluxDB token"
        >
          <Trash2Icon />
        </Button>
      </div>
    </div>

    <div class="grid gap-2 sm:grid-cols-2">
      <Button type="submit" disabled={saveDisabled}>
        <SaveIcon />
        Save
      </Button>
      <Button type="button" variant="outline" onclick={refreshInflux} disabled={actionDisabled}>
        <RefreshCwIcon class={influxBusy ? 'animate-spin' : ''} />
        Refresh
      </Button>
    </div>

    {#if influx?.lastError}
      <div class="rounded-md border border-amber-500/45 bg-amber-500/10 px-3 py-2 text-xs text-amber-900 dark:text-amber-100">
        <KeyRoundIcon class="mr-1 inline size-3 align-[-2px]" />
        {influx.lastError}
      </div>
    {/if}
  </form>
</DashboardCard>
