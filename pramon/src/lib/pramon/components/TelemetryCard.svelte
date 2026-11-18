<script lang="ts">
  import ActivityIcon from '@lucide/svelte/icons/activity'
  import type { TelemetryMetricRow, TelemetrySeriesKey, TelemetrySeriesPath } from '../types'
  import DashboardCard from './DashboardCard.svelte'

  let {
    subtitle,
    telemetryMetrics,
    hasTelemetryHistory,
    sparklineSeries,
    expandTelemetry,
  }: {
    subtitle: string
    telemetryMetrics: TelemetryMetricRow[]
    hasTelemetryHistory: boolean
    sparklineSeries: (key: TelemetrySeriesKey) => TelemetrySeriesPath[]
    expandTelemetry: (key: TelemetrySeriesKey) => void
  } = $props()
</script>

<DashboardCard
  title="Telemetry"
  description={subtitle}
  icon={ActivityIcon}
  contentClass="px-0 py-0 group-data-[size=sm]/card:px-0"
>
    <div class="metric-grid grid sm:grid-cols-2 lg:grid-cols-3">
      {#each telemetryMetrics as metric}
        <button
          type="button"
          class="relative min-h-32 cursor-pointer p-3 text-left transition-colors hover:bg-muted/40 focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring"
          onclick={() => expandTelemetry(metric.key)}
          disabled={!hasTelemetryHistory}
        >
          {#if sparklineSeries(metric.key).length > 0}
            <svg
              class="pointer-events-none absolute inset-x-0 bottom-0 h-20 w-full"
              viewBox="0 0 100 40"
              preserveAspectRatio="none"
              aria-hidden="true"
            >
              {#each sparklineSeries(metric.key) as item (item.slot)}
                <path d={item.area} fill={item.color} opacity="0.08" />
                <path d={item.path} fill="none" stroke={item.color} stroke-linecap="round" stroke-linejoin="round" stroke-width="1.7" vector-effect="non-scaling-stroke" />
              {/each}
            </svg>
          {/if}
          <div class="relative z-10 grid h-full content-between gap-5">
            <div class="flex items-center justify-between gap-3">
              <span class="text-xs font-semibold uppercase text-muted-foreground">{metric.label}</span>
              <span
                class="size-2 rounded-full bg-muted-foreground/30"
                class:bg-emerald-500={metric.tone === 'good'}
                class:bg-amber-500={metric.tone === 'warn'}
                class:bg-destructive={metric.tone === 'bad'}
              ></span>
            </div>
            <p
              class="font-mono text-lg font-semibold tabular-nums"
              class:text-muted-foreground={metric.tone === 'muted'}
              class:text-amber-700={metric.tone === 'warn'}
              class:text-destructive={metric.tone === 'bad'}
            >
              {metric.value}
            </p>
          </div>
        </button>
      {/each}
    </div>
</DashboardCard>
