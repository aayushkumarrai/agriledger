// Live feed client (spec §7). Reconnects with back-off. After any reconnect, and whenever the server says "resync",
// it first re-fetches current state through onResync(), and only then applies the live messages that arrived meanwhile.
// Status values for the small "reconnecting" note: "connecting", "live", "reconnecting".

export function liveUrl(loc = globalThis.location) {
  return `${loc.protocol === 'https:' ? 'wss' : 'ws'}://${loc.host}/live`;
}

export class LiveFeed {
  constructor({
    url = liveUrl(),
    WebSocketImpl = globalThis.WebSocket,
    onResync = async () => {},
    onStatus = () => {},
    backoffMs = [1000, 2000, 4000, 8000, 15000],
    setTimer = (fn, ms) => setTimeout(fn, ms),
    clearTimer = (id) => clearTimeout(id),
  } = {}) {
    Object.assign(this, { url, WebSocketImpl, onResync, onStatus, backoffMs, setTimer, clearTimer });
    this.handlers = new Map();
    this.socket = null;
    this.attempt = 0;
    this.timer = null;
    this.stopped = true;
    this.everOpened = false;
    this.resyncing = false;
    this.queue = [];
  }

  // live.on('stock_changed', (message) => ...). Several handlers per type are fine.
  on(type, handler) {
    if (!this.handlers.has(type)) this.handlers.set(type, []);
    this.handlers.get(type).push(handler);
    return this;
  }

  start() {
    if (!this.stopped) return;
    this.stopped = false;
    this.#connect();
  }

  stop() {
    this.stopped = true;
    if (this.timer !== null) { this.clearTimer(this.timer); this.timer = null; }
    if (this.socket) { this.socket.onclose = null; this.socket.close(); this.socket = null; }
  }

  #connect() {
    this.onStatus(this.everOpened ? 'reconnecting' : 'connecting');
    const socket = new this.WebSocketImpl(this.url);
    this.socket = socket;
    socket.onopen = () => {
      const isReconnect = this.everOpened;
      this.everOpened = true;
      this.attempt = 0;
      if (isReconnect) this.#resync(); else this.onStatus('live');
    };
    socket.onmessage = (event) => {
      let message;
      try { message = JSON.parse(event.data); } catch { return; }
      if (!message || typeof message.type !== 'string') return;
      if (message.type === 'resync') { this.#resync(); return; }
      if (this.resyncing) this.queue.push(message); else this.#dispatch(message);
    };
    socket.onclose = () => {
      if (this.stopped) return;
      this.socket = null;
      this.onStatus('reconnecting');
      const wait = this.backoffMs[Math.min(this.attempt, this.backoffMs.length - 1)];
      this.attempt += 1;
      this.timer = this.setTimer(() => { this.timer = null; if (!this.stopped) this.#connect(); }, wait);
    };
    socket.onerror = () => { /* onclose follows and handles the retry */ };
  }

  // Re-fetch current state, then resume: messages that came in during the fetch are applied afterwards, in order.
  async #resync() {
    if (this.resyncing) return;
    this.resyncing = true;
    try { await this.onResync(); } catch { /* the page shows its own error state; live updates resume anyway */ }
    this.resyncing = false;
    const waiting = this.queue.splice(0);
    for (const m of waiting) this.#dispatch(m);
    if (!this.stopped) this.onStatus('live');
  }

  #dispatch(message) {
    for (const h of this.handlers.get(message.type) || []) {
      try { h(message); } catch (e) { console.error('live handler failed', e); }
    }
  }
}
