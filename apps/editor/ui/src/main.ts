import './assets/main.css'

import { createApp } from 'vue'

// The editor started without a project shows the welcome window instead
// (index.html#welcome, development_plan.md 5.4); it needs none of the
// editor's modules.
if (location.hash === '#welcome') {
  import('./components/WelcomeApp.vue').then(({ default: WelcomeApp }) => createApp(WelcomeApp).mount('#app'))
} else {
  Promise.all([
    import('./App.vue'),
    import('./sceneMenus'), // viewport right-click menus
    import('./projects'), // File ▸ New / Open / Recent / Save As
    import('./editingMenus'), // copy/paste, resize, grid, delete prefab
    import('./exportGame'), // File ▸ Export Game…
    import('./scenes'), // File ▸ New Scene…, Scenes…
  ]).then(([{ default: App }]) => createApp(App).mount('#app'))
}
