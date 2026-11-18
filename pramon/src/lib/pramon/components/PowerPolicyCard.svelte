<script lang="ts">
  import PowerIcon from '@lucide/svelte/icons/power'
  import PowerOffIcon from '@lucide/svelte/icons/power-off'
  import ShieldCheckIcon from '@lucide/svelte/icons/shield-check'
  import SlidersHorizontalIcon from '@lucide/svelte/icons/sliders-horizontal'
  import type { PsuPowerPolicy } from '$lib/psu'
  import { Button } from '$lib/components/ui/button/index.js'
  import type { Tone } from '../types'
  import CompactStack from './CompactStack.svelte'
  import DashboardCard from './DashboardCard.svelte'

  let {
    busy,
    connected,
    canWrite,
    powerPolicy,
    description,
    setPowerPolicyOff,
    setPowerPolicyAutomatic,
    setPowerPolicyRedundancy,
    setPowerPolicyEfficiencyProfile,
    formatPowerWatts,
  }: {
    busy: string
    connected: boolean
    canWrite: boolean
    powerPolicy: PsuPowerPolicy | null
    description: string
    setPowerPolicyOff: () => void | Promise<void>
    setPowerPolicyAutomatic: (automatic: boolean) => void | Promise<void>
    setPowerPolicyRedundancy: (nRedundancy: number) => void | Promise<void>
    setPowerPolicyEfficiencyProfile: (efficiencyProfile: string) => void | Promise<void>
    formatPowerWatts: (value: number) => string
  } = $props()

  const REDUNDANCY_LEVELS = [0, 1, 2] as const

  let automatic = $derived(powerPolicy?.automatic ?? true)
  let nRedundancy = $derived(powerPolicy?.nRedundancy ?? 0)
  let allSlotsOff = $derived(powerPolicy != null && (
    powerPolicy.slots.length > 0
      ? powerPolicy.slots.every((slot) => !slot.psonAsserted)
      : powerPolicy.activeCount === 0
  ))
  let off = $derived(!automatic && allSlotsOff)
  let modeLabel = $derived(automatic ? 'Auto' : off ? 'Off' : 'Manual')
  let modeTone = $derived((automatic ? 'good' : off ? 'muted' : 'warn') as Tone)
  let limitTone = $derived((powerPolicy == null
    ? 'muted'
    : powerPolicy.limitExceeded
      ? 'bad'
      : powerPolicy.redundancyShortfall
        ? 'warn'
        : 'good') as Tone)
</script>

<DashboardCard title="Power Policy" description={description} icon={ShieldCheckIcon}>
  <div class="grid overflow-hidden rounded-[8px] border text-sm sm:grid-cols-2">
    <CompactStack label="Mode" value={modeLabel} tone={modeTone} />
    <CompactStack label="n_redundancy" value={String(nRedundancy)} tone={powerPolicy ? 'default' : 'muted'} />
    <CompactStack label="Load" value={powerPolicy ? formatPowerWatts(powerPolicy.totalOutputW) : 'n/a'} tone={limitTone} />
    <CompactStack label="Max" value={powerPolicy ? formatPowerWatts(powerPolicy.maxOutputW) : 'n/a'} tone={limitTone} />
    <CompactStack label="Active" value={powerPolicy ? `${powerPolicy.activeCount}/${powerPolicy.presentCount}` : 'n/a'} tone={powerPolicy ? 'default' : 'muted'} />
    <CompactStack label="Target" value={powerPolicy ? String(powerPolicy.targetActiveCount) : 'n/a'} tone={powerPolicy ? 'default' : 'muted'} />
    <CompactStack label="Profile" value={powerPolicy?.efficiencyProfileLabel ?? 'n/a'} tone={powerPolicy ? 'default' : 'muted'} />
  </div>

  <div class="grid gap-2">
    <div class="grid grid-cols-3 gap-2">
      <Button
        variant={automatic ? 'default' : 'outline'}
        onclick={() => setPowerPolicyAutomatic(true)}
        disabled={!canWrite || busy !== ''}
      >
        <PowerIcon />
        Auto
      </Button>

      <Button
        variant={!automatic && !off ? 'default' : 'outline'}
        onclick={() => setPowerPolicyAutomatic(false)}
        disabled={!canWrite || busy !== ''}
      >
        <SlidersHorizontalIcon />
        Manual
      </Button>

      <Button
        variant={off ? 'destructive' : 'outline'}
        onclick={setPowerPolicyOff}
        disabled={!canWrite || busy !== ''}
        class={off ? '' : 'border-destructive/45 text-destructive hover:bg-destructive/10'}
      >
        <PowerOffIcon />
        Off
      </Button>
    </div>

    <div class="grid grid-cols-3 gap-2">
      {#each REDUNDANCY_LEVELS as level}
        <Button
          variant={nRedundancy === level ? 'default' : 'outline'}
          onclick={() => setPowerPolicyRedundancy(level)}
          disabled={!canWrite || busy !== '' || nRedundancy === level}
        >
          N+{level}
        </Button>
      {/each}
    </div>

    {#if powerPolicy && powerPolicy.availableEfficiencyProfiles.length > 1}
      <div class="grid gap-2 sm:grid-cols-2">
        {#each powerPolicy.availableEfficiencyProfiles as profile}
          <Button
            variant={powerPolicy.efficiencyProfile === profile.id ? 'default' : 'outline'}
            onclick={() => setPowerPolicyEfficiencyProfile(profile.id)}
            disabled={!canWrite || busy !== '' || powerPolicy.efficiencyProfile === profile.id}
          >
            {profile.label}
          </Button>
        {/each}
      </div>
    {/if}
  </div>
</DashboardCard>
