<script setup lang="ts">
// A provider out of credits (the editor's AiClient read it in an answer's errors): one banner each, kept until closed,
// so a build that stalls on it says why (not only a row of failed items).
import { reactive } from 'vue'
import { onHostMessage } from '../ipc'
import { track } from '../activity'

interface Alert {
  provider: string
  text: string
  said: string
  at: number
  open: boolean
}

const alerts = reactive<Alert[]>([])

onHostMessage((message) => {
  if (message.type !== 'ai:credits') return
  const provider = String(message.provider ?? 'An AI provider')
  const text = String(message.text ?? `${provider} is out of credits`)
  const said = String(message.said ?? '')
  const was = alerts.find((a) => a.provider === provider)
  if (was) Object.assign(was, { text, said, at: Date.now() })
  else alerts.push({ provider, text, said, at: Date.now(), open: false })
  track(`credits-${provider}`, { title: `${provider}: out of credits`, detail: said || text, state: 'failed' })
})

function close(a: Alert): void {
  const at = alerts.indexOf(a)
  if (at >= 0) alerts.splice(at, 1)
}
</script>

<template>
  <div v-if="alerts.length" class="credit-banners">
    <div v-for="a in alerts" :key="a.provider" class="credit-banner" role="alert">
      <span class="mark">!</span>
      <div class="body">
        <div class="title">{{ a.provider }} is out of credits</div>
        <div class="text">{{ a.text }}</div>
        <button v-if="a.said" class="link" @click="a.open = !a.open">{{ a.open ? 'Hide' : 'Show' }} what it said</button>
        <pre v-if="a.open" class="said">{{ a.said }}</pre>
      </div>
      <button class="close" title="Close" @click="close(a)">×</button>
    </div>
  </div>
</template>

<style scoped>
.credit-banners {
  position: fixed;
  top: 34px;
  left: 50%;
  transform: translateX(-50%);
  width: min(640px, calc(100vw - 32px));
  z-index: 900;
  display: flex;
  flex-direction: column;
  gap: 6px;
}
.credit-banner {
  display: flex;
  gap: 10px;
  align-items: flex-start;
  padding: 10px 12px;
  background: #3a1f1f;
  border: 1px solid #c0504d;
  color: #f3e3e3;
  box-shadow: 0 4px 14px rgba(0, 0, 0, 0.45);
}
.mark {
  flex: none;
  width: 20px;
  height: 20px;
  display: grid;
  place-items: center;
  background: #c0504d;
  color: #fff;
  font-weight: 700;
}
.body {
  flex: 1;
  min-width: 0;
}
.title {
  font-weight: 600;
  margin-bottom: 2px;
}
.text {
  opacity: 0.9;
  line-height: 1.35;
}
.link {
  margin-top: 4px;
  padding: 0;
  background: none;
  border: none;
  color: #f0a9a7;
  cursor: pointer;
  text-decoration: underline;
}
.said {
  margin: 6px 0 0;
  padding: 6px 8px;
  max-height: 120px;
  overflow: auto;
  white-space: pre-wrap;
  word-break: break-word;
  background: rgba(0, 0, 0, 0.3);
  font-size: 11px;
}
.close {
  flex: none;
  background: none;
  border: none;
  color: #f3e3e3;
  font-size: 18px;
  line-height: 1;
  cursor: pointer;
}
</style>
