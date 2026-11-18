<script lang="ts">
  import EyeIcon from '@lucide/svelte/icons/eye'
  import EyeOffIcon from '@lucide/svelte/icons/eye-off'
  import RefreshCwIcon from '@lucide/svelte/icons/refresh-cw'
  import Trash2Icon from '@lucide/svelte/icons/trash-2'
  import UnplugIcon from '@lucide/svelte/icons/unplug'
  import WifiIcon from '@lucide/svelte/icons/wifi'
  import { Button } from '$lib/components/ui/button/index.js'
  import { Input } from '$lib/components/ui/input/index.js'
  import { Label } from '$lib/components/ui/label/index.js'
  import type { PsuWifiStatus } from '$lib/psu'
  import CompactStack from './CompactStack.svelte'
  import DashboardCard from './DashboardCard.svelte'

  let {
    connected,
    canWrite,
    busy,
    wifi,
    wifiSsid = $bindable<string>(),
    wifiPassword = $bindable<string>(),
    showWifiPassword = $bindable<boolean>(),
    wifiStatusLabel,
    wifiStatusTone,
    refreshWifi,
    connectWifi,
    reconnectWifi,
    disconnectWifi,
    forgetWifi,
  }: {
    connected: boolean
    canWrite: boolean
    busy: string
    wifi: PsuWifiStatus | null
    wifiSsid: string
    wifiPassword: string
    showWifiPassword: boolean
    wifiStatusLabel: string
    wifiStatusTone: 'default' | 'good' | 'warn' | 'bad' | 'muted'
    refreshWifi: () => void | Promise<void>
    connectWifi: (event: SubmitEvent) => void | Promise<void>
    reconnectWifi: () => void | Promise<void>
    disconnectWifi: () => void | Promise<void>
    forgetWifi: () => void | Promise<void>
  } = $props()

  let wifiBusy = $derived(busy.startsWith('WiFi'))
  let actionDisabled = $derived(!canWrite || busy !== '')
  let connectDisabled = $derived(actionDisabled || wifiSsid.length === 0)
  let apiStatus = $derived(wifi?.apiEnabled ? 'Enabled' : '-')
  let rssiLabel = $derived(wifi?.rssi == null ? '-' : `${wifi.rssi} dBm`)
</script>

<DashboardCard title="WiFi" description={wifiStatusLabel} icon={WifiIcon}>
  <div class="grid overflow-hidden rounded-[8px] border text-sm sm:grid-cols-2" aria-live="polite">
    <CompactStack label="State" value={wifiStatusLabel} tone={wifiStatusTone} />
    <CompactStack label="SSID" value={wifi?.ssid ?? '-'} tone={wifi?.ssid ? 'default' : 'muted'} />
    <CompactStack label="IP" value={wifi?.ip ?? '-'} tone={wifi?.ip ? 'default' : 'muted'} />
    <CompactStack label="RSSI" value={rssiLabel} tone={wifi?.rssi == null ? 'muted' : 'default'} />
    <CompactStack label="Hostname" value={wifi?.hostname ?? '-'} tone={wifi?.hostname ? 'default' : 'muted'} />
    <CompactStack label="API" value={apiStatus} tone={wifi?.apiEnabled ? 'good' : 'muted'} />
  </div>

  <form class="grid gap-3 border-t pt-4" onsubmit={connectWifi}>
    <div class="grid gap-2">
      <Label for="wifi-ssid">SSID</Label>
      <Input
        id="wifi-ssid"
        type="text"
        bind:value={wifiSsid}
        autocomplete="off"
        disabled={actionDisabled}
      />
    </div>
    <div class="grid gap-2">
      <Label for="wifi-password">Password</Label>
      <div class="flex gap-2">
        <Input
          id="wifi-password"
          type={showWifiPassword ? 'text' : 'password'}
          bind:value={wifiPassword}
          autocomplete="current-password"
          disabled={actionDisabled}
        />
        <Button
          type="button"
          variant="outline"
          size="icon"
          onclick={() => showWifiPassword = !showWifiPassword}
          disabled={actionDisabled}
          aria-label={showWifiPassword ? 'Hide WiFi password' : 'Show WiFi password'}
        >
          {#if showWifiPassword}
            <EyeOffIcon />
          {:else}
            <EyeIcon />
          {/if}
        </Button>
      </div>
    </div>
    <Button type="submit" disabled={connectDisabled}>
      <WifiIcon />
      Connect
    </Button>
  </form>

  <div class="grid gap-2 sm:grid-cols-2">
    <Button variant="outline" onclick={refreshWifi} disabled={actionDisabled}>
      <RefreshCwIcon class={wifiBusy ? 'animate-spin' : ''} />
      Refresh
    </Button>
    <Button variant="outline" onclick={reconnectWifi} disabled={actionDisabled || !wifi?.remembered}>
      <RefreshCwIcon />
      Reconnect
    </Button>
    <Button variant="outline" onclick={disconnectWifi} disabled={actionDisabled || !wifi?.connected}>
      <UnplugIcon />
      Disconnect
    </Button>
    <Button variant="outline" onclick={forgetWifi} disabled={actionDisabled || !wifi?.remembered}>
      <Trash2Icon />
      Forget
    </Button>
  </div>
</DashboardCard>
