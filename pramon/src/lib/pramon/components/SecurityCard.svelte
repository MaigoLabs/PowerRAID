<script lang="ts">
  import EyeIcon from '@lucide/svelte/icons/eye'
  import EyeOffIcon from '@lucide/svelte/icons/eye-off'
  import KeyRoundIcon from '@lucide/svelte/icons/key-round'
  import LogInIcon from '@lucide/svelte/icons/log-in'
  import LogOutIcon from '@lucide/svelte/icons/log-out'
  import ShieldCheckIcon from '@lucide/svelte/icons/shield-check'
  import ShieldOffIcon from '@lucide/svelte/icons/shield-off'
  import Trash2Icon from '@lucide/svelte/icons/trash-2'
  import { Button } from '$lib/components/ui/button/index.js'
  import { Input } from '$lib/components/ui/input/index.js'
  import { Label } from '$lib/components/ui/label/index.js'
  import type { PsuAuthStatus } from '$lib/psu'
  import CompactStack from './CompactStack.svelte'
  import DashboardCard from './DashboardCard.svelte'
  import type { Tone } from '../types'

  let {
    connected,
    busy,
    auth,
    canWrite,
    authenticated,
    showPassword = $bindable<boolean>(),
    passwordField = $bindable<string>(),
    confirmField = $bindable<string>(),
    submitPassword,
    login,
    logout,
    clearPassword,
    setTelemetryPublic,
    passwordError,
  }: {
    connected: boolean
    busy: string
    auth: PsuAuthStatus | null
    canWrite: boolean
    authenticated: boolean
    showPassword: boolean
    passwordField: string
    confirmField: string
    submitPassword: (event: SubmitEvent) => void | Promise<void>
    login: (password: string) => void | Promise<void>
    logout: () => void | Promise<void>
    clearPassword: () => void | Promise<void>
    setTelemetryPublic: (enabled: boolean) => void | Promise<void>
    passwordError: string
  } = $props()

  // Unlocking needs nothing but the password; setting one needs confirmation.
  let mode = $derived(auth?.configured === true && !authenticated ? 'unlock' : auth?.configured ? 'change' : 'set')
  let actionDisabled = $derived(!connected || busy !== '')
  let unlockDisabled = $derived(busy !== '' || passwordField.length === 0)
  let setDisabled = $derived(
    actionDisabled ||
      passwordField.length < (auth?.passwordMinLength ?? 8) ||
      passwordField !== confirmField,
  )
  let stateLabel = $derived(
    !connected
      ? 'Bridge disconnected'
      : auth == null
        ? 'Unknown (older firmware)'
        : !auth.configured
          ? 'Open - no password'
          : authenticated
            ? 'Unlocked'
            : 'Locked',
  )
  let stateTone = $derived((!connected || auth == null
    ? 'muted'
    : !auth.configured
      ? 'warn'
      : authenticated
        ? 'good'
        : 'muted') as Tone)
  let telemetryLabel = $derived(auth?.telemetryPublic ? 'Public' : 'Password required')
  let lengthHint = $derived(
    `Use at least ${auth?.passwordMinLength ?? 8} characters. The password is never sent to the bridge; it only stores a verifier.`,
  )
</script>

<DashboardCard title="Security" description={stateLabel} icon={ShieldCheckIcon}>
  <div class="grid overflow-hidden rounded-[8px] border text-sm sm:grid-cols-2" aria-live="polite">
    <CompactStack label="Password" value={auth?.configured ? 'Set' : 'Not set'} tone={auth?.configured ? 'good' : 'warn'} />
    <CompactStack label="This browser" value={authenticated ? 'Signed in' : auth?.configured ? 'Signed out' : 'Open access'} tone={authenticated ? 'good' : 'muted'} />
    <CompactStack label="Telemetry" value={telemetryLabel} tone={auth?.telemetryPublic ? 'warn' : 'good'} />
    <CompactStack label="State" value={stateLabel} tone={stateTone} />
  </div>

  {#if !connected}
    <p class="text-sm text-muted-foreground">Connect to the bridge to manage its password.</p>
  {:else if auth == null}
    <p class="text-sm text-muted-foreground">
      This firmware does not report login state, so password protection is unavailable.
    </p>
  {:else if mode === 'unlock'}
    <form
      class="grid gap-3 border-t pt-4"
      onsubmit={(event) => {
        event.preventDefault()
        void login(passwordField)
      }}
    >
      <div class="grid gap-2">
        <Label for="auth-unlock">Bridge password</Label>
        <div class="flex gap-2">
          <Input
            id="auth-unlock"
            type={showPassword ? 'text' : 'password'}
            bind:value={passwordField}
            autocomplete="current-password"
            disabled={busy !== ''}
          />
          <Button
            type="button"
            variant="outline"
            size="icon"
            onclick={() => showPassword = !showPassword}
            disabled={busy !== ''}
            aria-label={showPassword ? 'Hide password' : 'Show password'}
          >
            {#if showPassword}
              <EyeOffIcon />
            {:else}
              <EyeIcon />
            {/if}
          </Button>
        </div>
      </div>
      <Button type="submit" disabled={unlockDisabled}>
        <LogInIcon />
        Unlock
      </Button>
    </form>
  {:else}
    <form class="grid gap-3 border-t pt-4" onsubmit={submitPassword}>
      <div class="grid gap-2">
        <Label for="auth-new">{mode === 'set' ? 'New password' : 'Change password'}</Label>
        <div class="flex gap-2">
          <Input
            id="auth-new"
            type={showPassword ? 'text' : 'password'}
            bind:value={passwordField}
            autocomplete="new-password"
            disabled={busy !== ''}
          />
          <Button
            type="button"
            variant="outline"
            size="icon"
            onclick={() => showPassword = !showPassword}
            disabled={busy !== ''}
            aria-label={showPassword ? 'Hide password' : 'Show password'}
          >
            {#if showPassword}
              <EyeOffIcon />
            {:else}
              <EyeIcon />
            {/if}
          </Button>
        </div>
      </div>
      <div class="grid gap-2">
        <Label for="auth-confirm">Confirm password</Label>
        <Input
          id="auth-confirm"
          type="password"
          bind:value={confirmField}
          autocomplete="new-password"
          disabled={busy !== ''}
        />
      </div>
      <p class="text-xs text-muted-foreground">{lengthHint}</p>
      <Button type="submit" disabled={setDisabled}>
        <KeyRoundIcon />
        {mode === 'set' ? 'Set password' : 'Change password'}
      </Button>
    </form>
  {/if}

  {#if passwordError}
    <div class="rounded-md border border-amber-500/45 bg-amber-500/10 px-3 py-2 text-xs text-amber-900 dark:text-amber-100">
      {passwordError}
    </div>
  {/if}

  {#if auth?.configured}
    <div class="grid gap-3 border-t pt-4">
      <label class="flex items-center justify-between gap-3 text-sm">
        <span class="grid gap-0.5">
          <span class="font-medium">Telemetry viewable without a password</span>
          <span class="text-xs text-muted-foreground">
            Readings stay visible; switches and settings still need a login.
          </span>
        </span>
        <input
          class="size-4 accent-primary"
          type="checkbox"
          checked={auth.telemetryPublic}
          disabled={actionDisabled || !canWrite}
          onchange={(event) => void setTelemetryPublic(event.currentTarget.checked)}
        />
      </label>

      <div class="grid gap-2 sm:grid-cols-2">
        {#if authenticated}
          <Button variant="outline" onclick={logout} disabled={busy !== ''}>
            <LogOutIcon />
            Sign out
          </Button>
        {/if}
        <Button
          variant="destructive"
          onclick={clearPassword}
          disabled={actionDisabled || !canWrite}
          class={authenticated ? '' : 'sm:col-span-2'}
        >
          <Trash2Icon />
          Remove password
        </Button>
      </div>
      <p class="flex items-start gap-2 text-xs text-muted-foreground">
        <ShieldOffIcon class="mt-0.5 size-3.5 shrink-0" />
        Removing the password reopens the bridge to anyone on the network. A USB serial
        <code class="mx-1 rounded bg-muted px-1 py-0.5 font-mono">auth reset</code>
        always works as recovery.
      </p>
    </div>
  {/if}
</DashboardCard>
