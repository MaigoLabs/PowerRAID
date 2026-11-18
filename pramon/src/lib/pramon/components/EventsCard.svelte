<script lang="ts">
  import EraserIcon from '@lucide/svelte/icons/eraser'
  import ShieldAlertIcon from '@lucide/svelte/icons/shield-alert'
  import { Button } from '$lib/components/ui/button/index.js'
  import type { CompactRowData, PsControlActionRow, PsControlSystemTypeRow } from '../types'
  import CompactRow from './CompactRow.svelte'
  import DashboardCard from './DashboardCard.svelte'

  let {
    eventRows,
    connected,
    canWrite,
    busy,
    psControlWord,
    psControlActionRows,
    psControlSystemTypeRows,
    clearInterrupt,
    hexWord,
    applyPsControlAction,
    setPsControlSystemType,
  }: {
    eventRows: CompactRowData[]
    connected: boolean
    canWrite: boolean
    busy: string
    psControlWord: number | null
    psControlActionRows: PsControlActionRow[]
    psControlSystemTypeRows: PsControlSystemTypeRow[]
    clearInterrupt: () => void | Promise<void>
    hexWord: (value: number) => string
    applyPsControlAction: (action: PsControlActionRow) => void | Promise<void>
    setPsControlSystemType: (systemType: PsControlSystemTypeRow) => void | Promise<void>
  } = $props()
</script>

<DashboardCard
  title="Events"
  description="Shutdown, warning, interrupt, control"
  icon={ShieldAlertIcon}
  contentClass="grid gap-0 p-0"
>
    <div class="grid text-sm">
      {#each eventRows as row}
        <CompactRow {...row} />
      {/each}
    </div>
    <div class="flex flex-wrap gap-2 border-t p-4">
      <Button variant="outline" onclick={clearInterrupt} disabled={!canWrite || busy !== ''}>
        <EraserIcon />
        Clear
      </Button>
    </div>
    <div class="grid gap-3 border-t p-4">
      <div class="flex flex-wrap items-center justify-between gap-2">
        <span class="text-sm font-medium">PS control actions</span>
        <span class="font-mono text-xs text-muted-foreground">
          {psControlWord == null ? '0x3A not sampled' : hexWord(psControlWord)}
        </span>
      </div>
      <div class="flex flex-wrap gap-2">
        {#each psControlActionRows as action}
          <Button
            size="xs"
            variant="outline"
            onclick={() => applyPsControlAction(action)}
            disabled={action.disabled}
            class={action.className}
          >
            {action.label}
          </Button>
        {/each}
      </div>
      <div class="grid gap-2">
        <span class="text-xs font-semibold uppercase text-muted-foreground">System type</span>
        <div class="flex flex-wrap gap-2">
          {#each psControlSystemTypeRows as systemType}
            <Button
              size="xs"
              variant="outline"
              onclick={() => setPsControlSystemType(systemType)}
              disabled={systemType.disabled}
              class={systemType.className}
            >
              {systemType.label}
            </Button>
          {/each}
        </div>
      </div>
    </div>
</DashboardCard>
