<script lang="ts">
  import ClockIcon from '@lucide/svelte/icons/clock'
  import PlugIcon from '@lucide/svelte/icons/plug'
  import { Button } from '$lib/components/ui/button/index.js'
  import { Input } from '$lib/components/ui/input/index.js'
  import { Label } from '$lib/components/ui/label/index.js'
  import * as Select from '$lib/components/ui/select/index.js'
  import DashboardCard from './DashboardCard.svelte'

  let {
    webApiBaseUrl = $bindable<string>(),
    connected,
    busy,
    connectionDescription,
    autoRefreshEnabled = $bindable<boolean>(),
    autoRefreshMs = $bindable<string>(),
    safeAutoRefreshIntervalMs,
    fastCadence = $bindable<boolean>(),
    connect,
    disconnect,
    handleFastCadenceChange,
    formatNumber,
  }: {
    webApiBaseUrl: string
    connected: boolean
    busy: string
    connectionDescription: string
    autoRefreshEnabled: boolean
    autoRefreshMs: string
    safeAutoRefreshIntervalMs: number
    fastCadence: boolean
    connect: () => void | Promise<void>
    disconnect: () => void | Promise<void>
    handleFastCadenceChange: (event: Event) => void
    formatNumber: (value: number, digits: number) => string
  } = $props()

  let connectDisabled = $derived(busy !== '' || webApiBaseUrl.trim().length === 0)
</script>

<DashboardCard title="Connection" description={connectionDescription} icon={PlugIcon} descriptionClass="line-clamp-2 break-words">
    <div class="grid gap-2">
      <Label for="web-api-base">API base URL</Label>
      <Input
        id="web-api-base"
        type="url"
        bind:value={webApiBaseUrl}
        placeholder="http://192.168.137.246/api/v1"
        disabled={connected || busy !== ''}
      />
    </div>

    <div class="grid grid-cols-2 gap-2">
      {#if connected}
        <Button variant="outline" onclick={disconnect} disabled={busy !== ''} class="col-span-2">
          <PlugIcon />
          Disconnect
        </Button>
      {:else}
        <Button onclick={connect} disabled={connectDisabled} class="col-span-2">
          <PlugIcon />
          Connect bridge
        </Button>
      {/if}
    </div>

    <div class="grid gap-3 rounded-[8px] border p-3">
      <label class="flex items-center justify-between gap-3 text-sm">
        <span class="flex items-center gap-2 font-medium">
          <ClockIcon class="size-4 text-muted-foreground" />
          Register monitor
        </span>
        <input
          class="size-4 accent-primary"
          type="checkbox"
          bind:checked={autoRefreshEnabled}
          disabled={!connected}
        />
      </label>
      <div class="grid gap-2">
        <Label>Frequency</Label>
        <Select.Root type="single" bind:value={autoRefreshMs} disabled={!connected || !autoRefreshEnabled}>
          <Select.Trigger class="w-full">
            Every {formatNumber(safeAutoRefreshIntervalMs / 1000, safeAutoRefreshIntervalMs === 1000 ? 0 : 1)}s
          </Select.Trigger>
          <Select.Content>
            <Select.Item value="1000">Every 1s</Select.Item>
            <Select.Item value="2000">Every 2s</Select.Item>
            <Select.Item value="5000">Every 5s</Select.Item>
            <Select.Item value="10000">Every 10s</Select.Item>
          </Select.Content>
        </Select.Root>
      </div>
      <label class="flex items-start justify-between gap-3 border-t pt-3 text-sm">
        <span class="grid gap-1">
          <span class="font-medium">Fast I2C cadence</span>
          <span class="text-xs text-muted-foreground">Skip the 80 ms PSU sequence wait.</span>
        </span>
        <input
          class="mt-0.5 size-4 accent-amber-500"
          type="checkbox"
          bind:checked={fastCadence}
          onchange={handleFastCadenceChange}
          disabled={!connected}
        />
      </label>
    </div>
  </DashboardCard>
