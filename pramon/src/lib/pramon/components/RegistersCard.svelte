<script lang="ts">
  import EyeIcon from '@lucide/svelte/icons/eye'
  import EyeOffIcon from '@lucide/svelte/icons/eye-off'
  import PenLineIcon from '@lucide/svelte/icons/pen-line'
  import RefreshCwIcon from '@lucide/svelte/icons/refresh-cw'
  import RotateCcwIcon from '@lucide/svelte/icons/rotate-ccw'
  import TableIcon from '@lucide/svelte/icons/table'
  import { Badge } from '$lib/components/ui/badge/index.js'
  import { Button } from '$lib/components/ui/button/index.js'
  import { Input } from '$lib/components/ui/input/index.js'
  import type { PsuRegisterRead, RegisterOption } from '$lib/psu'
  import type { RegisterTableRow } from '../types'
  import CompactStack from './CompactStack.svelte'
  import DashboardCard from './DashboardCard.svelte'
  import RegisterMeaning from './RegisterMeaning.svelte'

  let {
    registerRows,
    listenedRegisterCount,
    readResult,
    selectedAddress,
    registerWriteValues,
    connected,
    canRead,
    canWrite,
    busy,
    registerAddress,
    toggleRegisterListen,
    readRegisterValue,
    isResetOnlyRegister,
    resetRegisterValue,
    writeRegisterValue,
    updateRegisterWriteValue,
    registerWritePlaceholder,
    registerWriteUnit,
  }: {
    registerRows: RegisterTableRow[]
    listenedRegisterCount: number
    readResult: PsuRegisterRead | null
    selectedAddress: string
    registerWriteValues: Record<string, string>
    connected: boolean
    canRead: boolean
    canWrite: boolean
    busy: string
    registerAddress: (register: RegisterOption) => string
    toggleRegisterListen: (register: RegisterOption) => void
    readRegisterValue: (register: RegisterOption) => void | Promise<void>
    isResetOnlyRegister: (register: RegisterOption) => boolean
    resetRegisterValue: (register: RegisterOption) => void | Promise<void>
    writeRegisterValue: (event: SubmitEvent, register: RegisterOption) => void | Promise<void>
    updateRegisterWriteValue: (register: string, event: Event) => void
    registerWritePlaceholder: (register: RegisterOption) => string
    registerWriteUnit: (register: RegisterOption) => string
  } = $props()
</script>

<DashboardCard
  title="Registers"
  description={`${listenedRegisterCount} listened · sorted LiteOn uC map`}
  icon={TableIcon}
>
    <div class="grid gap-3 lg:grid-cols-[1fr_auto] lg:items-start">
      <div class="grid overflow-hidden rounded-[8px] border text-sm sm:grid-cols-4">
        <CompactStack label="Last register" value={readResult?.register ?? '-'} tone={readResult ? 'default' : 'muted'} />
        <CompactStack label="Value" value={readResult?.value ?? '-'} tone={readResult ? 'default' : 'muted'} />
        <CompactStack label="Selected address" value={selectedAddress} tone={selectedAddress !== '-' ? 'default' : 'muted'} />
        <CompactStack label="Listening" value={`${listenedRegisterCount}`} tone={listenedRegisterCount > 0 ? 'good' : 'warn'} />
      </div>
    </div>

    <div class="overflow-auto rounded-[8px] border">
      <table class="w-full min-w-[1020px] border-collapse text-left text-sm">
        <thead class="bg-muted/55 text-xs uppercase text-muted-foreground">
          <tr>
            <th class="w-px whitespace-nowrap px-1 py-2 font-semibold" aria-label="Address"></th>
            <th class="w-8 px-1 py-2 text-center font-semibold" aria-label="Listen"></th>
            <th class="w-px whitespace-nowrap px-1 py-2 font-semibold" aria-label="Access"></th>
            <th class="px-3 py-2 font-semibold">Register</th>
            <th class="w-48 px-3 py-2 font-semibold">Value</th>
            <th class="w-36 px-3 py-2 font-semibold">Last update</th>
            <th class="px-3 py-2 font-semibold">Meaning</th>
            <th class="w-[200px] px-3 py-2 text-right font-semibold">Action</th>
          </tr>
        </thead>
        <tbody>
          {#each registerRows as row (row.register.value)}
            {@const register = row.register}
            <tr class="border-t align-middle">
              <td class="w-px whitespace-nowrap px-1 py-2 font-mono text-xs text-muted-foreground">{registerAddress(register)}</td>
              <td class="px-1 py-2 text-center">
                <Button
                  size="icon-xs"
                  variant="ghost"
                  onclick={() => toggleRegisterListen(register)}
                  disabled={busy !== ''}
                  aria-label={row.listened ? `Stop listening to ${register.label}` : `Listen to ${register.label}`}
                  class={row.listened ? 'text-primary hover:text-primary' : 'text-muted-foreground'}
                >
                  {#if row.listened}
                    <EyeIcon />
                  {:else}
                    <EyeOffIcon />
                  {/if}
                </Button>
              </td>
              <td class="w-px whitespace-nowrap px-1 py-2">
                <Badge
                  variant={register.access === 'RW' ? 'destructive' : 'secondary'}
                  class="h-5 rounded-md px-1.5 font-mono text-[10px]"
                >
                  {register.access}
                </Badge>
              </td>
              <td class="px-3 py-2">
                <span class="font-medium">{register.label}</span>
              </td>
              <td class="px-3 py-2">
                <div class="grid gap-0.5">
                  {#if row.parsedValue}
                    <span class="text-xs font-semibold">{row.parsedValue}</span>
                    <span class="font-mono text-xs text-muted-foreground tabular-nums">{row.liveValue}</span>
                  {:else}
                    <span class="font-mono text-xs font-semibold tabular-nums">{row.liveValue}</span>
                  {/if}
                </div>
              </td>
              <td
                class="px-3 py-2 font-mono text-xs tabular-nums"
                class:text-muted-foreground={row.updateTone === 'muted'}
                class:text-emerald-700={row.updateTone === 'good'}
                class:text-amber-700={row.updateTone === 'warn'}
              >
                {row.updateText}
              </td>
              <td class="px-3 py-2 text-xs text-muted-foreground">
                <RegisterMeaning {register} />
              </td>
              <td class="px-3 py-2">
                <div class="flex items-center justify-end gap-2">
                  <Button
                    size="icon-xs"
                    variant="outline"
                    onclick={() => readRegisterValue(register)}
                    disabled={!canRead || busy !== ''}
                    aria-label={`Read ${register.label}`}
                    title={`Read ${register.label}`}
                  >
                    <RefreshCwIcon />
                  </Button>
                  {#if register.access === 'RW'}
                    {#if isResetOnlyRegister(register)}
                      <Button
                        size="xs"
                        variant="outline"
                        onclick={() => resetRegisterValue(register)}
                        disabled={!canWrite || busy !== ''}
                        aria-label={`Reset ${register.label}`}
                        title={`Reset ${register.label}`}
                      >
                        <RotateCcwIcon />
                        Reset
                      </Button>
                    {:else}
                      {@const writeValue = registerWriteValues[register.value] ?? ''}
                      {@const writeUnit = registerWriteUnit(register)}
                      <form class="flex items-center justify-end gap-1" onsubmit={(event) => writeRegisterValue(event, register)}>
                        <div class="relative">
                          <Input
                            value={writeValue}
                            oninput={(event) => updateRegisterWriteValue(register.value, event)}
                            placeholder={registerWritePlaceholder(register)}
                            title={register.help.format}
                            aria-label={`Write value for ${register.label}`}
                            disabled={busy !== ''}
                            class={writeUnit ? 'h-7 w-28 pr-11 font-mono text-xs' : 'h-7 w-28 font-mono text-xs'}
                          />
                          {#if writeUnit}
                            <span class="pointer-events-none absolute inset-y-0 right-2 flex items-center justify-end text-[10px] font-medium text-muted-foreground">
                              {writeUnit}
                            </span>
                          {/if}
                        </div>
                        {#if writeValue.trim().length > 0}
                          <Button
                            type="submit"
                            size="icon-xs"
                            variant="destructive"
                            disabled={!canWrite || busy !== ''}
                            aria-label={`Write ${register.label}`}
                            title={`Write ${register.label}`}
                          >
                            <PenLineIcon />
                          </Button>
                        {/if}
                      </form>
                    {/if}
                  {/if}
                </div>
              </td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>
</DashboardCard>
