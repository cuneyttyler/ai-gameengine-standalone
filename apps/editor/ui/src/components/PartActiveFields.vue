<script setup lang="ts">
// A part's settings for while it's active (Animate a part, the building editor, the Inspector): how long it plays once
// activated, the clip the player plays meanwhile (once, or again and again), and whether the player is held still.
import { onMounted } from 'vue'
import { playerClips, requestPlayerClips } from '../inputActions'

defineProps<{ duration: number; playerClip: string; playerLoop: boolean; holdsPlayer: boolean; disabled?: boolean }>()
const emit = defineEmits<{ (e: 'set', key: 'duration' | 'playerClip' | 'playerLoop' | 'holdsPlayer', value: number | string | boolean): void }>()

onMounted(() => {
  if (!playerClips.value.length) requestPlayerClips()
})
</script>

<template>
  <div class="active-fields">
    <label title="Once activated, it plays this long, then stops by itself (0: until it's used again, or left)">
      Duration (s)
      <input type="number" min="0" step="0.5" :value="duration" :disabled="disabled"
             @change="emit('set', 'duration', Math.max(0, Number(($event.target as HTMLInputElement).value) || 0))" />
    </label>
    <label title="A clip the player plays while it's active (the characters' clips)">
      Player clip
      <select :value="playerClip" :disabled="disabled" @change="emit('set', 'playerClip', ($event.target as HTMLSelectElement).value)">
        <option value="">none</option>
        <option v-if="playerClip && !playerClips.includes(playerClip)" :value="playerClip">{{ playerClip }} (missing)</option>
        <option v-for="c in playerClips" :key="c" :value="c">{{ c }}</option>
      </select>
    </label>
    <label v-if="playerClip" class="inline" title="The player's clip again and again while it's active (else once)">
      <input type="checkbox" :checked="playerLoop" :disabled="disabled" @change="emit('set', 'playerLoop', ($event.target as HTMLInputElement).checked)" />
      Player's clip plays continuously</label>
    <label class="inline" title="The player can't move while it's active: until it's used again or its duration is over">
      <input type="checkbox" :checked="holdsPlayer" :disabled="disabled" @change="emit('set', 'holdsPlayer', ($event.target as HTMLInputElement).checked)" />
      Player can't move while it's active</label>
  </div>
</template>

<style scoped>
.active-fields {
  display: flex;
  flex-direction: column;
  gap: 4px;
}
.active-fields label {
  display: flex;
  justify-content: space-between;
  align-items: center;
  gap: 8px;
}
.active-fields label.inline {
  justify-content: flex-start;
}
.active-fields input[type='number'] {
  width: 70px;
}
</style>
