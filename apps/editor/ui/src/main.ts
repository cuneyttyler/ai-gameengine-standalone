import './assets/main.css'

import { createApp } from 'vue'
import App from './App.vue'
import './sceneMenus' // viewport right-click menus
import './projects' // File ▸ New / Open / Recent / Save As
import './editingMenus' // copy/paste, resize, grid, delete prefab

createApp(App).mount('#app')
