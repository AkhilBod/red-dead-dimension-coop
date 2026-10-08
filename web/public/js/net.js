// The one connection to the server: rooms, the duel's state, and the server's clock. Reconnects by itself and takes
// back the same seat (the token lives in this tab's sessionStorage, so a refresh is not a new player).

const tokenKey = (code) => `rdd:token:${code}`;
const store = {
  get(key) { try { return sessionStorage.getItem(key) || ''; } catch { return ''; } },
  set(key, value) { try { sessionStorage.setItem(key, value); } catch { /* private mode */ } },
  drop(key) { try { sessionStorage.removeItem(key); } catch { /* private mode */ } },
};

export class Net {
  constructor(handlers) {
    this.on = handlers;               // { state, joined, shot, error, status, replaced }
    this.ws = null;
    this.code = '';
    this.name = '';
    this.offset = 0;                  // server time = Date.now() + offset
    this.rtt = 120;
    this.bestRtt = Infinity;
    this.retries = 0;
    this.wanted = false;              // stay connected (we are in a room)
    setInterval(() => this.ping(), 2000);
    document.addEventListener('visibilitychange', () => { if (!document.hidden && this.wanted && !this.open()) { this.connect(); } });
  }

  serverNow() { return Date.now() + this.offset; }
  open() { return this.ws && this.ws.readyState === WebSocket.OPEN; }

  connect() {
    if (this.ws && this.ws.readyState <= WebSocket.OPEN) { return this.ready; }
    const ws = new WebSocket(`${location.protocol === 'https:' ? 'wss' : 'ws'}://${location.host}/ws`);
    this.ws = ws;
    this.on.status(this.retries ? 'reconnecting' : 'connecting');
    this.ready = new Promise((resolve) => {
      ws.onopen = () => {
        this.retries = 0;
        this.ping();
        // Back into the room we were in (a refresh, a dropped connection, a phone that slept).
        if (this.wanted && this.code) { this.send({ t: 'join', code: this.code, name: this.name, token: store.get(tokenKey(this.code)) }); }
        resolve();
      };
    });
    ws.onmessage = (e) => this.receive(JSON.parse(e.data));
    ws.onclose = () => {
      if (this.ws !== ws) { return; }
      this.ws = null;
      if (!this.wanted) { return; }
      this.on.status('reconnecting');
      setTimeout(() => { if (this.wanted && !this.ws) { this.connect(); } }, Math.min(500 * 2 ** this.retries++, 5000));
    };
    return this.ready;
  }

  send(msg) {
    if (this.open()) { this.ws.send(JSON.stringify(msg)); return true; }
    return false;
  }

  ping() {
    this.send({ t: 'ping', c: performance.now(), rtt: Math.round(this.rtt) });
  }

  receive(msg) {
    switch (msg.t) {
      case 'pong': {
        const now = performance.now();
        const rtt = now - msg.c;
        this.rtt = this.rtt * 0.7 + rtt * 0.3;
        // The clock offset from the quickest round trip seen: the one with the least queueing in it.
        if (rtt <= this.bestRtt * 1.2) {
          this.bestRtt = Math.min(this.bestRtt, rtt);
          this.offset = msg.s + rtt / 2 - Date.now();
        }
        break;
      }
      case 'joined':
        this.code = msg.code;
        this.wanted = true;
        store.set(tokenKey(msg.code), msg.token);
        this.on.status('ok');
        this.on.joined(msg);
        break;
      case 'state':
        this.on.state(msg);
        break;
      case 'shot':
        this.on.shot(msg);
        break;
      case 'error':
        if (msg.error === 'no_room' || msg.error === 'full' || msg.error === 'bad_code') { this.wanted = false; }
        this.on.error(msg);
        break;
      case 'replaced':
        this.wanted = false;
        this.on.replaced();
        break;
      default:
        break;
    }
  }

  async create(name) {
    this.name = name;
    await this.connect();
    this.send({ t: 'create', name });
  }

  async join(code, name) {
    this.name = name;
    this.code = code;
    await this.connect();
    this.send({ t: 'join', code, name, token: store.get(tokenKey(code)) });
  }

  leave() {
    this.send({ t: 'leave' });
    if (this.code) { store.drop(tokenKey(this.code)); }
    this.wanted = false;
    this.code = '';
  }
}
