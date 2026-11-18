<script lang="ts">
  import type { Component, Snippet } from 'svelte'
  import * as Card from '$lib/components/ui/card/index.js'

  let {
    title,
    description,
    icon: Icon,
    titleClass = '',
    descriptionClass = '',
    contentClass = 'grid gap-4 py-4',
    headerClass = 'relative overflow-hidden border-b py-4 pr-20',
    iconClass = 'pointer-events-none absolute right-4 top-1/2 -translate-y-1/2 text-muted-foreground/10',
    headerControls,
    children,
  }: {
    title: string
    description: string | null | undefined
    icon: Component<{ class?: string }>
    titleClass?: string
    descriptionClass?: string
    contentClass?: string
    headerClass?: string
    iconClass?: string
    headerControls?: Snippet
    children?: Snippet
  } = $props()
</script>

<Card.Root class="rounded-[8px] gap-0 py-0" size="sm">
  <Card.Header class={headerClass}>
    <Card.Title class={titleClass}>{title}</Card.Title>
    <Card.Description class={descriptionClass}>{description}</Card.Description>
    {@render headerControls?.()}
    <Card.Action class={iconClass}>
      <Icon class="size-16 stroke-[1.4]" />
    </Card.Action>
  </Card.Header>
  <Card.Content class={contentClass}>
    {@render children?.()}
  </Card.Content>
</Card.Root>
