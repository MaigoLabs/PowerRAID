<script lang="ts">
  import XIcon from '@lucide/svelte/icons/x'
  import { Button } from '$lib/components/ui/button/index.js'
  import type { ChartTick, TelemetryMetricRow, TelemetrySeriesKey, TelemetrySeriesPath } from '../types'

  let {
    metric,
    telemetryHistoryLength,
    latestTelemetryValue,
    detailedChartYTicks,
    detailedChartSeries,
    telemetryUnit,
    close,
  }: {
    metric: TelemetryMetricRow
    telemetryHistoryLength: number
    latestTelemetryValue: (key: TelemetrySeriesKey) => string
    detailedChartYTicks: (key: TelemetrySeriesKey) => ChartTick[]
    detailedChartSeries: (key: TelemetrySeriesKey) => TelemetrySeriesPath[]
    telemetryUnit: (key: TelemetrySeriesKey) => string
    close: () => void
  } = $props()
</script>

<div
  class="fixed inset-0 z-50 grid place-items-center bg-background/80 p-3 backdrop-blur-sm"
  role="presentation"
  onclick={(event) => {
    if (event.target === event.currentTarget) {
      close()
    }
  }}
>
  <div
    class="w-full max-w-5xl overflow-hidden rounded-[8px] border bg-card shadow-lg"
    role="dialog"
    aria-modal="true"
    aria-labelledby="telemetry-detail-title"
  >
    <header class="flex items-start justify-between gap-4 border-b p-4">
      <div class="min-w-0">
        <h2 id="telemetry-detail-title" class="text-base font-semibold">{metric.label}</h2>
        <p class="mt-1 text-sm text-muted-foreground">
          Last {latestTelemetryValue(metric.key)} · {telemetryHistoryLength} samples
        </p>
      </div>
      <Button variant="ghost" size="icon-sm" onclick={close} aria-label="Close telemetry graph">
        <XIcon />
      </Button>
    </header>

    <div class="p-4">
      <div class="rounded-[8px] border bg-background/60 p-3">
        <svg class="h-[360px] w-full overflow-visible" viewBox="0 0 640 340" role="img" aria-label={`${metric.label} telemetry graph`}>
          <rect x="56" y="32" width="528" height="256" fill="transparent" />
          {#each detailedChartYTicks(metric.key) as tick}
            <line x1="56" y1={tick.y} x2="584" y2={tick.y} stroke="currentColor" stroke-width="0.7" class="text-border" vector-effect="non-scaling-stroke" />
            <text x="48" y={tick.y + 4} text-anchor="end" class="fill-muted-foreground text-[11px]">{tick.label}</text>
          {/each}
          {#each [0, 5, 10, 15, 20] as seconds}
            <line x1={56 + (seconds / 20) * 528} y1="32" x2={56 + (seconds / 20) * 528} y2="288" stroke="currentColor" stroke-width="0.7" class="text-border" vector-effect="non-scaling-stroke" />
            <text x={56 + (seconds / 20) * 528} y="314" text-anchor="middle" class="fill-muted-foreground text-[11px]">
              {seconds === 20 ? 'now' : `-${20 - seconds}s`}
            </text>
          {/each}
          <line x1="56" y1="288" x2="584" y2="288" stroke="currentColor" stroke-width="1" class="text-muted-foreground" vector-effect="non-scaling-stroke" />
          <line x1="56" y1="32" x2="56" y2="288" stroke="currentColor" stroke-width="1" class="text-muted-foreground" vector-effect="non-scaling-stroke" />
          <text x="320" y="336" text-anchor="middle" class="fill-muted-foreground text-xs">Last 20 seconds</text>
          <text x="16" y="160" text-anchor="middle" class="fill-muted-foreground text-xs" transform="rotate(-90 16 160)">
            {telemetryUnit(metric.key)}
          </text>
          {#each detailedChartSeries(metric.key) as item (item.slot)}
            <path d={item.area} fill={item.color} opacity="0.06" />
            <path
              d={item.path}
              fill="none"
              stroke={item.color}
              stroke-linecap="round"
              stroke-linejoin="round"
              stroke-width="2.4"
              vector-effect="non-scaling-stroke"
            />
          {/each}
        </svg>
      </div>
      <div class="mt-3 flex flex-wrap items-center gap-4 text-sm">
        {#each detailedChartSeries(metric.key) as item (item.slot)}
          <span class="inline-flex items-center gap-2 font-medium">
            <span class="h-0.5 w-8 rounded" style={`background-color: ${item.color}`}></span>
            {item.label}
            <span class="font-mono text-muted-foreground">{item.latest}</span>
          </span>
        {/each}
        {#if detailedChartSeries(metric.key).length === 0}
          <span class="font-mono text-muted-foreground">{latestTelemetryValue(metric.key)}</span>
        {/if}
      </div>
    </div>
  </div>
</div>
