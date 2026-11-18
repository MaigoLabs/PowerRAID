<script lang="ts">
  import AlertTriangleIcon from '@lucide/svelte/icons/alert-triangle'
  import CableIcon from '@lucide/svelte/icons/cable'
  import CircleCheckIcon from '@lucide/svelte/icons/circle-check'
  import RefreshCwIcon from '@lucide/svelte/icons/refresh-cw'
  import { Badge } from '$lib/components/ui/badge/index.js'
  import type { Tone } from '../types'

  let {
    connected,
    connectionState,
    connectionLabel,
    connectionTone,
  }: {
    connected: boolean
    connectionState: string
    connectionLabel: string
    connectionTone: Tone
  } = $props()
</script>

<header class="grid gap-4 rounded-[8px] border bg-card px-4 py-4 shadow-xs sm:px-5 lg:grid-cols-[1fr_auto] lg:items-center">
  <div class="min-w-0">
    <div class="flex flex-wrap items-center gap-2">
      <Badge variant="outline" class="h-6 rounded-md px-2">PowerRAID</Badge>
      <Badge variant="secondary" class="h-6 rounded-md px-2">LiteOn PS-2122-3H</Badge>
    </div>
    <h1 class="mt-3 text-2xl font-semibold leading-tight tracking-normal sm:text-3xl">pramon</h1>
    <p class="mt-1 max-w-3xl text-sm text-muted-foreground">
      Browser monitor and control surface for the LiteOn bridge.
    </p>
  </div>
  <div class="flex flex-wrap items-center gap-2 lg:justify-end">
    <Badge
      variant={connectionTone === 'bad' ? 'destructive' : connected ? 'default' : 'secondary'}
      class="h-8 rounded-md px-3"
    >
      {#if connectionState === 'busy'}
        <RefreshCwIcon class="animate-spin" />
      {:else if connected}
        <CircleCheckIcon />
      {:else if connectionState === 'unavailable'}
        <AlertTriangleIcon />
      {:else}
        <CableIcon />
      {/if}
      {connectionLabel}
    </Badge>
  </div>
</header>
