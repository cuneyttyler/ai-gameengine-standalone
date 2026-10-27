import './assets/main.css'

import { createApp } from 'vue'
import { sendToHost } from './ipc'

// An error before the UI is up would leave a blank window: tell the host,
// which shows it. Later errors are only logged by the host.
let mounted = false
function report(text: string): void {
  sendToHost({ type: 'ui:error', text, fatal: !mounted })
}
window.addEventListener('error', (event) => report(`${event.message} (${event.filename}:${event.lineno})`))
window.addEventListener('unhandledrejection', (event) => report(String(event.reason?.stack ?? event.reason)))

// The editor started without a project shows the welcome window instead
// (index.html#welcome, development_plan.md 5.4); it needs none of the
// editor's modules.
if (location.hash === '#welcome') {
  import('./components/WelcomeApp.vue')
    .then(({ default: WelcomeApp }) => {
      createApp(WelcomeApp).mount('#app')
      mounted = true
    })
    .catch((error) => report(String(error?.stack ?? error)))
} else {
  Promise.all([
    import('./App.vue'),
    import('./sceneMenus'), // viewport right-click menus
    import('./projects'), // File ▸ New / Open / Recent / Save As
    import('./editingMenus'), // copy/paste, resize, grid, delete prefab
    import('./exportGame'), // File ▸ Export Game…
    import('./scenes'), // File ▸ New Scene…, Scenes…
    import('./recovery'), // a crash's unsaved changes offered back
  ])
    .then(([{ default: App }]) => {
      createApp(App).mount('#app')
      mounted = true
    })
    .catch((error) => report(String(error?.stack ?? error)))
}
