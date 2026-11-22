import { spawn } from 'node:child_process';
import { existsSync, mkdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';

const root = fileURLToPath(new URL('../', import.meta.url));
const app = `${root}dist/app`;
const executable = `${app}/Canvas.Server${process.platform === 'win32' ? '.exe' : ''}`;
const dataDir = `${root}.local/data`;
const url = 'http://localhost:5180';

if (!existsSync(executable)) {
  console.error('Canvas is not built yet. Run: pnpm build:app');
  process.exit(1);
}

mkdirSync(dataDir, { recursive: true });

const server = spawn(executable, [], {
  cwd: app,
  stdio: 'inherit',
  env: {
    ...process.env,
    ASPNETCORE_URLS: url,
    ASPNETCORE_ENVIRONMENT: 'Production',
    Storage__Database: `${dataDir}/canvas.db`,
    Storage__Fonts: `${dataDir}/fonts`,
  },
});

server.on('exit', (code) => process.exit(code ?? 0));
process.on('SIGINT', () => server.kill());
