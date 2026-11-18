<script lang="ts">
  import KeyRoundIcon from '@lucide/svelte/icons/key-round'
  import ShieldCheckIcon from '@lucide/svelte/icons/shield-check'
  import XIcon from '@lucide/svelte/icons/x'
  import { Button } from '$lib/components/ui/button/index.js'
  import { Input } from '$lib/components/ui/input/index.js'
  import { Label } from '$lib/components/ui/label/index.js'

  let {
    minLength,
    busy,
    passwordError,
    save,
    dismiss,
  }: {
    minLength: number
    busy: string
    passwordError: string
    save: (password: string) => void | Promise<void>
    dismiss: () => void
  } = $props()

  let password = $state('')
  let confirm = $state('')
  let submitDisabled = $derived(busy !== '' || password.length < minLength || password !== confirm)
</script>

<div class="fixed inset-0 z-50 grid place-items-center bg-background/80 p-3 backdrop-blur-sm" role="presentation">
  <div
    class="w-full max-w-md overflow-hidden rounded-[8px] border bg-card shadow-lg"
    role="dialog"
    aria-modal="true"
    aria-labelledby="first-run-title"
  >
    <header class="relative overflow-hidden border-b p-4 pr-12">
      <h2 id="first-run-title" class="text-base font-semibold">Protect this bridge</h2>
      <p class="mt-1 text-sm text-muted-foreground">
        Anyone who can reach this bridge can switch your PSUs on and off. Set a password now,
        or skip and keep it open on a trusted network.
      </p>
      <button
        type="button"
        class="absolute right-3 top-3 rounded-md p-1 text-muted-foreground transition hover:bg-muted hover:text-foreground"
        onclick={dismiss}
        aria-label="Skip for now"
      >
        <XIcon class="size-4" />
      </button>
    </header>

    <form
      class="grid gap-3 p-4"
      onsubmit={(event) => {
        event.preventDefault()
        void save(password)
      }}
    >
      <div class="grid gap-2">
        <Label for="first-run-password">Password</Label>
        <Input
          id="first-run-password"
          type="password"
          bind:value={password}
          autocomplete="new-password"
          disabled={busy !== ''}
        />
      </div>
      <div class="grid gap-2">
        <Label for="first-run-confirm">Confirm password</Label>
        <Input
          id="first-run-confirm"
          type="password"
          bind:value={confirm}
          autocomplete="new-password"
          disabled={busy !== ''}
        />
      </div>
      <p class="text-xs text-muted-foreground">
        Use at least {minLength} characters. The bridge stores only a verifier, never the password,
        and the password is not transmitted.
      </p>

      {#if passwordError}
        <div class="rounded-md border border-amber-500/45 bg-amber-500/10 px-3 py-2 text-xs text-amber-900 dark:text-amber-100">
          {passwordError}
        </div>
      {/if}

      <Button type="submit" disabled={submitDisabled}>
        <ShieldCheckIcon />
        Set password
      </Button>
      <Button type="button" variant="ghost" onclick={dismiss} disabled={busy !== ''}>
        <KeyRoundIcon />
        Keep it open for now
      </Button>
    </form>
  </div>
</div>
