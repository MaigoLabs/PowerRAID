<script lang="ts">
  import RouterIcon from '@lucide/svelte/icons/router'
  import type { PsuFirmwareInfo, PsuMonitorState } from '$lib/psu'
  import CompactStack from './CompactStack.svelte'
  import DashboardCard from './DashboardCard.svelte'

  let {
    busy,
    connected,
    bridgeFirmwareLabel,
    monitor,
    transportLabel,
    transportDetailLabel,
    transportDetail,
    firmwareInfo,
  }: {
    busy: string
    connected: boolean
    bridgeFirmwareLabel: string
    monitor: PsuMonitorState | null
    transportLabel: string
    transportDetailLabel: string
    transportDetail: string
    firmwareInfo: PsuFirmwareInfo | null
  } = $props()
</script>

<DashboardCard title="Bridge" description={busy || 'Transport and firmware'} icon={RouterIcon}>
  <div class="grid overflow-hidden rounded-[8px] border text-sm sm:grid-cols-2" aria-live="polite">
    <CompactStack label="Bridge" value={bridgeFirmwareLabel} tone={firmwareInfo ? 'default' : 'muted'} />
    <CompactStack label="Monitor" value={monitor?.active ? `${monitor.intervalMs} ms` : 'Stopped'} tone={monitor?.active ? 'good' : 'muted'} />
    <CompactStack label="Transport" value={transportLabel} tone={connected ? 'good' : 'muted'} />
    <CompactStack label={transportDetailLabel} value={transportDetail} tone={transportDetail === '-' ? 'muted' : 'default'} />
    <CompactStack label="Connected FW" value={firmwareInfo?.firmware ?? '-'} tone={firmwareInfo ? 'default' : 'muted'} />
    <CompactStack label="Build" value={firmwareInfo?.build ?? '-'} tone={firmwareInfo ? 'default' : 'muted'} />
  </div>
</DashboardCard>
