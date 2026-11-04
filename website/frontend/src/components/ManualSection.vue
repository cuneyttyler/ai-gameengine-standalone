<script setup>
defineProps({ data: Object })
</script>

<template>
  <section id="manual">
    <div class="wrap">
      <div class="section-head reveal">
        <span class="eyebrow">By hand</span>
        <h2>{{ data.title }}</h2>
        <p>{{ data.lead }}</p>
      </div>
      <div class="grid four">
        <div v-for="group in data.groups" :key="group.name" class="card group reveal">
          <h3>{{ group.name }}</h3>
          <ul>
            <li v-for="item in group.items" :key="item"><span class="cube"></span>{{ item }}</li>
          </ul>
        </div>
      </div>
    </div>
  </section>
</template>

<style scoped>
.group h3 { font-size: 16px; padding-bottom: 12px; border-bottom: 1px solid var(--line); margin-bottom: 14px; }
.group ul { list-style: none; margin: 0; padding: 0; display: grid; gap: 10px; }
.group li { display: flex; gap: 10px; align-items: flex-start; color: var(--muted); font-size: 14px; line-height: 1.5; }
.group .cube { width: 8px; height: 8px; margin-top: 7px; }
</style>
