import { createApp } from 'vue'
import App from './App.vue'
import DocsApp from './docs/DocsApp.vue'
import './style.css'

// /docs and its pages: the documentation (data/help); everything else: the product site.
const docs = location.pathname === '/docs' || location.pathname.startsWith('/docs/')
createApp(docs ? DocsApp : App).mount('#app')
