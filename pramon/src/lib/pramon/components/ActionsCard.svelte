<script lang="ts">
  import EraserIcon from '@lucide/svelte/icons/eraser'
  import ToggleLeftIcon from '@lucide/svelte/icons/toggle-left'
  import ToggleRightIcon from '@lucide/svelte/icons/toggle-right'
  import WrenchIcon from '@lucide/svelte/icons/wrench'
  import { Button } from '$lib/components/ui/button/index.js'
  import CompactStack from './CompactStack.svelte'
  import DashboardCard from './DashboardCard.svelte'

  let {
    busy,
    connected,
    canWrite,
    selectedSlotLabel,
    selectedSlotStatus,
    selectedAddress,
    psonDriveAsserted,
    psonToggleLabel,
    togglePson,
    clearInterrupt,
  }: {
    busy: string
    connected: boolean
    canWrite: boolean
    selectedSlotLabel: string
    selectedSlotStatus: string
    selectedAddress: string
    psonDriveAsserted: boolean
    psonToggleLabel: string
    togglePson: () => void | Promise<void>
    clearInterrupt: () => void | Promise<void>
  } = $props()
</script>

<DashboardCard title="Selected PSU" description={selectedSlotLabel} icon={WrenchIcon}>
    <div class="grid overflow-hidden rounded-[8px] border text-sm sm:grid-cols-3">
      <CompactStack label="Slot" value={selectedSlotLabel} tone={connected ? 'default' : 'muted'} />
      <CompactStack label="State" value={selectedSlotStatus} tone={connected ? 'default' : 'muted'} />
      <CompactStack label="Address" value={selectedAddress} tone={selectedAddress !== '-' ? 'default' : 'muted'} />
    </div>

    <div class="grid gap-2 sm:grid-cols-2">
      <Button
        variant="outline"
        onclick={togglePson}
        disabled={!canWrite || busy !== ''}
        class="border-amber-500/45 bg-amber-500/10 text-amber-800 hover:bg-amber-500/20 dark:text-amber-200"
      >
        {#if psonDriveAsserted}
          <ToggleRightIcon />
        {:else}
          <ToggleLeftIcon />
        {/if}
        {psonToggleLabel}
      </Button>
      <Button variant="outline" onclick={clearInterrupt} disabled={!canWrite || busy !== ''}>
        <EraserIcon />
        Clear interrupt
      </Button>
    </div>
</DashboardCard>
