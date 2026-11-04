import { defineConfig } from 'vite'
import vue from '@vitejs/plugin-vue'

// In development the Flask server (website/backend/app.py, port 5000) answers /api.
export default defineConfig({
  plugins: [vue()],
  server: {
    port: 5173,
    proxy: { '/api': 'http://127.0.0.1:5000' },
  },
})
