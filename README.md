<<<<<<< HEAD
# INSpace CanSat GS

Desktop ground-station app for the INSpace CanSat workflow.

## Start here
- [Simple run guide](docs/USER_RUN_GUIDE.md)
- [Architecture and implementation guide](docs/ARCHITECTURE_AND_IMPLEMENTATION.md)
- [Hardware integration guide](docs/HARDWARE_INTEGRATION.md)
- [Competition checklist](docs/COMPETITION_CHECKLIST.md)

## Quick start
1. Install dependencies:

```bash
cd frontend
npm install
```

2. Start the Electron app in development mode:

```bash
cd frontend
npm run electron:dev
```

3. If you want live telemetry, start the Python backend from the repository root in another terminal.

## Useful scripts
- `cd frontend && npm run dev` starts only the Vite renderer
- `cd frontend && npm run electron:dev` starts Vite and Electron together
- `cd frontend && npm run build` builds the renderer for production
- `cd frontend && npm run electron:build` builds the app and packages it with Electron Builder
- `cd frontend && npm run dist` creates a distributable build

## Project layout
- `frontend/` contains the Electron main process, Vite app, and React dashboard
- `backend/` contains the Python telemetry backend
- `docs/` contains user-facing guides and system documentation

## Notes
- Use mock mode in the backend if hardware is not connected
- The Electron app expects telemetry through the Python backend during normal operation
=======
# React + TypeScript + Vite

This template provides a minimal setup to get React working in Vite with HMR and some ESLint rules.

Currently, two official plugins are available:

- [@vitejs/plugin-react](https://github.com/vitejs/vite-plugin-react/blob/main/packages/plugin-react) uses [Oxc](https://oxc.rs)
- [@vitejs/plugin-react-swc](https://github.com/vitejs/vite-plugin-react/blob/main/packages/plugin-react-swc) uses [SWC](https://swc.rs/)

## React Compiler

The React Compiler is not enabled on this template because of its impact on dev & build performances. To add it, see [this documentation](https://react.dev/learn/react-compiler/installation).

## Expanding the ESLint configuration

If you are developing a production application, we recommend updating the configuration to enable type-aware lint rules:

```js
export default defineConfig([
  globalIgnores(['dist']),
  {
    files: ['**/*.{ts,tsx}'],
    extends: [
      // Other configs...

      // Remove tseslint.configs.recommended and replace with this
      tseslint.configs.recommendedTypeChecked,
      // Alternatively, use this for stricter rules
      tseslint.configs.strictTypeChecked,
      // Optionally, add this for stylistic rules
      tseslint.configs.stylisticTypeChecked,

      // Other configs...
    ],
    languageOptions: {
      parserOptions: {
        project: ['./tsconfig.node.json', './tsconfig.app.json'],
        tsconfigRootDir: import.meta.dirname,
      },
      // other options...
    },
  },
])
```

You can also install [eslint-plugin-react-x](https://github.com/Rel1cx/eslint-react/tree/main/packages/plugins/eslint-plugin-react-x) and [eslint-plugin-react-dom](https://github.com/Rel1cx/eslint-react/tree/main/packages/plugins/eslint-plugin-react-dom) for React-specific lint rules:

```js
// eslint.config.js
import reactX from 'eslint-plugin-react-x'
import reactDom from 'eslint-plugin-react-dom'

export default defineConfig([
  globalIgnores(['dist']),
  {
    files: ['**/*.{ts,tsx}'],
    extends: [
      // Other configs...
      // Enable lint rules for React
      reactX.configs['recommended-typescript'],
      // Enable lint rules for React DOM
      reactDom.configs.recommended,
    ],
    languageOptions: {
      parserOptions: {
        project: ['./tsconfig.node.json', './tsconfig.app.json'],
        tsconfigRootDir: import.meta.dirname,
      },
      // other options...
    },
  },
])
```
>>>>>>> upstream/Daksh
