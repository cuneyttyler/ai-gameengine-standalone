import { createApp } from 'vue'
import App from './App.vue'
import BetaApp from './beta/BetaApp.vue'
import DocsApp from './docs/DocsApp.vue'
import './style.css'

// /docs and its pages: the documentation (data/help); /beta: the beta and registering; everything else: the site.
const path = location.pathname
const under = (root) => path === root || path.startsWith(root + '/')
createApp(under('/docs') ? DocsApp : under('/beta') ? BetaApp : App).mount('#app')
