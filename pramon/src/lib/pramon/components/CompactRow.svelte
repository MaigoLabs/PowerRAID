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

<div class="grid grid-cols-[max-content_minmax(0,1fr)] items-center gap-3 border-b px-3 py-2 last:border-b-0">
  <span class="text-xs font-semibold uppercase text-muted-foreground">{label}</span>
  <span
    class="min-w-0 break-words text-right font-mono font-semibold leading-tight [overflow-wrap:anywhere]"
    class:text-muted-foreground={tone === 'muted'}
    class:text-emerald-700={tone === 'good'}
    class:text-amber-700={tone === 'warn'}
    class:text-destructive={tone === 'bad'}
  >
    {value}
  </span>
</div>
