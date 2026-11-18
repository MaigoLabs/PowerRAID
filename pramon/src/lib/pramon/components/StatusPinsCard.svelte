<script lang="ts">
  import GaugeIcon from '@lucide/svelte/icons/gauge'
  import type { MetricRow } from '../types'
  import DashboardCard from './DashboardCard.svelte'
  import MetricCell from './MetricCell.svelte'

  let {
    subtitle,
    statusMetrics,
    pinRows,
  }: {
    subtitle: string
    statusMetrics: MetricRow[]
    pinRows: MetricRow[]
  } = $props()
</script>

<DashboardCard
  title="Status & Pins"
  description={subtitle}
  icon={GaugeIcon}
  contentClass="px-0 py-0 group-data-[size=sm]/card:px-0"
>
    <div class="metric-grid grid sm:grid-cols-2 lg:grid-cols-3">
      {#each statusMetrics as metric}
        <MetricCell {...metric} />
      {/each}
      {#each pinRows as metric}
        <MetricCell {...metric} />
      {/each}
    </div>
</DashboardCard>
