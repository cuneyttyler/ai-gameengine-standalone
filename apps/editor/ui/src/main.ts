import './assets/main.css'

import { createApp } from 'vue'
import App from './App.vue'
import './sceneMenus' // viewport right-click menus
import './projects' // File ▸ New / Open / Recent / Save As
import './editingMenus' // copy/paste, resize, grid, delete prefab
import './exportGame' // File ▸ Export Game…
import './scenes' // File ▸ New Scene…, Scenes…

createApp(App).mount('#app')
