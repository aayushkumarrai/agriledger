import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join } from 'node:path';
import { webcrypto } from 'node:crypto';

export const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..', '..');
export const WEB = join(ROOT, 'web');
export const messages = JSON.parse(readFileSync(join(WEB, 'i18n', 'en.json'), 'utf8'));
if (!globalThis.crypto) globalThis.crypto = webcrypto;   // Node 18 has no crypto global without a flag
