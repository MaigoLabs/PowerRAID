<script lang="ts">
  import type { Tone } from '../types'

  let {
    label,
    value,
    detail,
    tone,
  }: {
    label: string
    value: string
    detail: string
    tone: Tone
  } = $props()
</script>

<div class="grid min-h-32 content-between gap-5 p-3">
  <div class="flex items-start justify-between gap-3">
    <div class="min-w-0">
      <p class="truncate text-xs font-semibold uppercase text-muted-foreground">{label}</p>
      <p class="mt-1 truncate text-xs text-muted-foreground">{detail}</p>
    </div>
    <span
      class="mt-1 size-2 rounded-full bg-muted-foreground/30"
      class:bg-emerald-500={tone === 'good'}
      class:bg-amber-500={tone === 'warn'}
      class:bg-destructive={tone === 'bad'}
    ></span>
  </div>
  <p
    class="min-w-0 truncate font-mono text-lg font-semibold tabular-nums"
    class:text-muted-foreground={tone === 'muted'}
    class:text-emerald-700={tone === 'good'}
    class:text-amber-700={tone === 'warn'}
    class:text-destructive={tone === 'bad'}
  >
    {value}
  </p>
</div>
