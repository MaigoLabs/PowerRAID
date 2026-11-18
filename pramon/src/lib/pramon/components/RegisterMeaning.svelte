<script lang="ts">
  import type { RegisterOption } from '$lib/psu'

  let { register }: { register: RegisterOption } = $props()
</script>

<details class="grid gap-2">
  <summary
    class="cursor-pointer list-none text-left leading-relaxed text-muted-foreground underline decoration-border underline-offset-2 transition-colors hover:text-foreground focus-visible:text-foreground focus-visible:outline-none [&::-webkit-details-marker]:hidden"
  >
    {register.description}
  </summary>
  <div class="mt-2 rounded-[6px] border border-border/70 bg-muted/25 px-2 py-1.5">
    <div class="grid gap-2 text-[11px] leading-relaxed text-muted-foreground">
      <p class="text-foreground">{register.help.summary}</p>

      {#if register.help.format}
        <p><span class="font-semibold text-foreground">Format:</span> {register.help.format}</p>
      {/if}

      {#if register.help.defaultValue}
        <p><span class="font-semibold text-foreground">Default:</span> {register.help.defaultValue}</p>
      {/if}

      {#if register.help.legalValues && register.help.legalValues.length > 0}
        <div>
          <span class="font-semibold text-foreground">Legal writes / limits</span>
          <ul class="mt-1 grid gap-1 pl-4">
            {#each register.help.legalValues as item}
              <li class="list-disc">{item}</li>
            {/each}
          </ul>
        </div>
      {/if}

      {#if register.help.readBehavior && register.help.readBehavior.length > 0}
        <div>
          <span class="font-semibold text-foreground">Read behavior</span>
          <ul class="mt-1 grid gap-1 pl-4">
            {#each register.help.readBehavior as item}
              <li class="list-disc">{item}</li>
            {/each}
          </ul>
        </div>
      {/if}

      {#if register.help.writeBehavior && register.help.writeBehavior.length > 0}
        <div>
          <span class="font-semibold text-foreground">Write behavior</span>
          <ul class="mt-1 grid gap-1 pl-4">
            {#each register.help.writeBehavior as item}
              <li class="list-disc">{item}</li>
            {/each}
          </ul>
        </div>
      {/if}

      {#if register.help.bitFields && register.help.bitFields.length > 0}
        <div class="grid gap-1">
          <span class="font-semibold text-foreground">Bits</span>
          <div class="grid gap-1">
            {#each register.help.bitFields as field}
              <div class="grid grid-cols-[3.5rem_minmax(0,1fr)] gap-2">
                <span class="font-mono text-muted-foreground">{field.bits}</span>
                <span>
                  <span class="font-medium text-foreground">{field.name}:</span>
                  {field.description}
                </span>
              </div>
            {/each}
          </div>
        </div>
      {/if}

      {#if register.help.notes && register.help.notes.length > 0}
        <div>
          <span class="font-semibold text-foreground">Notes</span>
          <ul class="mt-1 grid gap-1 pl-4">
            {#each register.help.notes as item}
              <li class="list-disc">{item}</li>
            {/each}
          </ul>
        </div>
      {/if}

      <p><span class="font-semibold text-foreground">Source:</span> {register.help.source}</p>
    </div>
  </div>
</details>
