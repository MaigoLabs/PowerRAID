<script lang="ts">
  import type { Tone } from '../types'

  let {
    label,
    value,
    tone,
  }: {
    label: string
    value: string
    tone: Tone
  } = $props()
</script>

<div class="grid gap-1 border-b border-r-0 px-3 py-2 last:border-b-0 sm:border-r sm:[&:nth-child(2n)]:border-r-0 sm:[&:nth-last-child(-n+2)]:border-b-0 lg:[&:nth-child(2n)]:border-r lg:[&:nth-child(4n)]:border-r-0 lg:[&:nth-last-child(-n+4)]:border-b-0">
  <span class="text-xs font-semibold uppercase text-muted-foreground">{label}</span>
  <span
    class="font-mono font-semibold"
    class:text-muted-foreground={tone === 'muted'}
    class:text-emerald-700={tone === 'good'}
    class:text-amber-700={tone === 'warn'}
    class:text-destructive={tone === 'bad'}
  >
    {value}
  </span>
</div>
