import crypto from "node:crypto";
import { normIp, isIPv4, isLoopback, isLocal, isPrivate, splitHostPort } from "./addr";

/** Join codes: short enough to say over voice chat, from letters and digits nobody mixes up (no 0 O 1 I). */
export const CODE_ALPHABET = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
export const CODE_LENGTH = 5;
export const isCode = (s: string) => new RegExp(`^[${CODE_ALPHABET}]{${CODE_LENGTH}}$`).test(s.toUpperCase());

/** How often a host's game says it is still there, and how long the lobby waits before it drops a silent one. */
export const HEARTBEAT_S = 10;
export const EXPIRE_S = 30;
/** Per source address: registrations a minute, and games listed at once. */
export const REGISTER_PER_MINUTE = 6;
export const GAMES_PER_ADDRESS = 4;

/** What a host's game tells the lobby (POST /games, then PUT /games/<code> with the token). */
export type GameFields = {
  name: string;      // the host's name, as its panel or profile says
  monster: string;   // the host's monster, "" in the menus
  city: string;      // "Chicago 1": the city and block, "" in the menus
  players: number;
  max: number;
  reserved: number;  // player slots kept for a player who dropped out (60 s, for their rejoin): not open to others
  port: number;      // the host's ENet port (UDP)
  build: string;     // 8 hex digits: net.c's build id; guest and host must match or the host refuses the hello
};

/** One listed game. `source` is where its registration came from; `external` what UPnP (or the player) said the
 *  host's public address is; `lan` the host's own address on its network, for guests on that same network. */
export type Game = GameFields & {
  code: string; token: string;
  source: string; external: string; lan: string;
  since: number; seen: number;
};

/** What everyone sees of a game: no token, no source. `address` is the public one ("" when the lobby can't tell:
 *  a host on the lobby's own PC or network with no UPnP), `lan` the local one ("" none). */
export type PublicGame = GameFields & { code: string; address: string; lan: string; since: number };

/** Typed by players and sent by games: no control characters, no tabs (games.txt's separator), not too long. */
const clean = (s: unknown, n: number) => String(s ?? "").replace(/[\x00-\x1f\x7f]/g, " ").trim().slice(0, n);

function int(v: unknown, lo: number, hi: number, dflt?: number): number {
  if ((v === undefined || v === "") && dflt !== undefined) return dflt;
  const n = Number(v);
  if (!Number.isInteger(n) || n < lo || n > hi) throw new Error(`a number from ${lo} to ${hi}`);
  return n;
}

/** A registration or a heartbeat's body, checked. `partial`: a heartbeat may leave anything out. */
export function parseFields(b: Record<string, unknown>, partial = false): Partial<GameFields> & { external?: string; lan?: string } {
  const out: Partial<GameFields> & { external?: string; lan?: string } = {};
  const has = (k: string) => b[k] !== undefined && b[k] !== null;
  const field = (k: string, f: () => void) => {
    try { f(); } catch (e: any) { throw new Error(`${k}: ${e.message}`); }
  };
  if (has("name") || !partial) out.name = clean(b.name, 24) || "Rampage host";
  if (has("monster") || !partial) out.monster = clean(b.monster, 24);
  if (has("city") || !partial) out.city = clean(b.city, 32);
  if (has("players") || !partial) field("players", () => { out.players = int(b.players, 0, 4, 1); });
  if (has("max") || !partial) field("max", () => { out.max = int(b.max, 1, 4, 2); });
  if (has("reserved") || !partial) field("reserved", () => { out.reserved = int(b.reserved, 0, 3, 0); });
  if (has("port") || !partial) field("port", () => { out.port = int(b.port, 1, 65535); });
  if (has("build") || !partial) field("build", () => {
    const s = String(b.build ?? "");
    if (!/^[0-9A-Fa-f]{8}$/.test(s)) throw new Error("8 hex digits");
    out.build = s.toUpperCase();
  });
  if (has("external") && b.external !== "") field("external", () => {
    const hp = splitHostPort(String(b.external));
    if (!hp) throw new Error("not a host or host:port");
    out.external = hp.port ? `${hp.host}:${hp.port}` : hp.host;
  });
  if (has("lan") && b.lan !== "") field("lan", () => {
    const s = String(b.lan);
    if (!isPrivate(s) && !isLoopback(s)) throw new Error("not a local IPv4 address");
    out.lan = s;
  });
  if (out.players !== undefined && out.max !== undefined && out.players > out.max) out.players = out.max;
  if (out.reserved !== undefined && out.players !== undefined && out.max !== undefined && out.players + out.reserved > out.max)
    out.reserved = Math.max(0, out.max - out.players);
  return out;
}

export class LobbyError extends Error { constructor(public status: number, message: string) { super(message); } }

type Listener = { add(g: PublicGame): void; update(g: PublicGame): void; remove(code: string): void };

/** Every listed game, in memory: a lobby that restarts forgets them, and their next heartbeat (a 404) makes each
 *  host's game register again. `now` is injectable for the tests. */
export class Games {
  private games = new Map<string, Game>();
  private recent = new Map<string, number[]>();   // per source address: when it registered, for the rate limit
  listeners: Listener[] = [];

  constructor(private opts: { publicAddress?: string; now?: () => number } = {}) {}
  private now() { return this.opts.now ? this.opts.now() : Date.now(); }

  register(body: Record<string, unknown>, from: string): Game {
    const source = normIp(from);
    const f = parseFields(body) as GameFields & { external?: string; lan?: string };
    const t = this.now();
    const times = (this.recent.get(source) ?? []).filter(x => t - x < 60_000);
    if (times.length >= REGISTER_PER_MINUTE) throw new LobbyError(429, "too many games registered from your address, try again in a minute");
    times.push(t);
    this.recent.set(source, times);
    // The same host starting again (same place, same port): its old listing goes rather than lingering 30 seconds.
    for (const g of [...this.games.values()])
      if (g.source === source && g.port === f.port && g.lan === (f.lan ?? g.lan)) this.remove(g.code);
    if ([...this.games.values()].filter(g => g.source === source).length >= GAMES_PER_ADDRESS)
      throw new LobbyError(429, `at most ${GAMES_PER_ADDRESS} games from one address`);
    let code: string;
    do code = Array.from(crypto.randomBytes(CODE_LENGTH), b => CODE_ALPHABET[b % CODE_ALPHABET.length]).join("");
    while (this.games.has(code));
    const g: Game = { ...f, code, token: crypto.randomBytes(16).toString("hex"), source, external: f.external ?? "",
                      lan: f.lan ?? (isPrivate(source) ? source : ""), since: t, seen: t };
    this.games.set(code, g);
    for (const l of this.listeners) l.add(this.public(g));
    return g;
  }

  /** The token's game, or a LobbyError: 404 gone (register again), 403 someone else's. */
  private own(code: string, token: unknown): Game {
    const g = this.games.get(code.toUpperCase());
    if (!g) throw new LobbyError(404, "no such game (it expired: register again)");
    const a = Buffer.from(String(token ?? "")), b = Buffer.from(g.token);
    if (a.length !== b.length || !crypto.timingSafeEqual(a, b)) throw new LobbyError(403, "not your game");
    return g;
  }

  heartbeat(code: string, body: Record<string, unknown>): Game {
    const g = this.own(code, body.token);
    const f = parseFields(body, true);
    Object.assign(g, f, { seen: this.now() });
    if (g.players > g.max) g.players = g.max;
    if (g.players + g.reserved > g.max) g.reserved = Math.max(0, g.max - g.players);
    for (const l of this.listeners) l.update(this.public(g));
    return g;
  }

  unregister(code: string, token: unknown) {
    this.remove(this.own(code, token).code);
  }

  private remove(code: string) {
    if (!this.games.delete(code)) return;
    for (const l of this.listeners) l.remove(code);
  }

  /** Drops the games silent for EXPIRE_S; the server calls it every few seconds. */
  sweep() {
    const t = this.now();
    for (const g of [...this.games.values()]) if (t - g.seen > EXPIRE_S * 1000) this.remove(g.code);
    for (const [k, v] of this.recent) if (!v.some(x => t - x < 60_000)) this.recent.delete(k);
  }

  get(code: string) { return this.games.get(code.toUpperCase()); }
  list(): Game[] { return [...this.games.values()].sort((a, b) => a.since - b.since); }

  /** The host's public address: what UPnP said, else where it registered from when that is on the internet, else
   *  the lobby's own publicAddress (config.json) when the host is on the lobby's PC or network; "" not known. */
  publicAddress(g: Game): string {
    const withPort = (h: string) => (h.includes(":") ? h : `${h}:${g.port}`);
    if (g.external) return withPort(g.external);
    if (!isLocal(g.source)) return withPort(g.source);
    if (this.opts.publicAddress) return withPort(this.opts.publicAddress);
    return "";
  }

  public(g: Game): PublicGame {
    const { code, name, monster, city, players, max, reserved, port, build, since } = g;
    return { code, name, monster, city, players, max, reserved, port, build, since, address: this.publicAddress(g),
             lan: g.lan ? `${g.lan}:${g.port}` : "" };
  }

  /** The address a guest asking from `from` should join, "ip:port":
   *   - the host on the lobby's PC and so is the guest: 127.0.0.1
   *   - the guest on the host's network (the same public address, or both on the lobby's network): the host's LAN address
   *   - otherwise the public one; failing that the LAN address (it may still work: a VPN, a tailnet) */
  joinFor(g: Game, from: string): string {
    const r = normIp(from), s = g.source;
    const lan = g.lan ? `${g.lan}:${g.port}` : "";
    if (isLoopback(r) && isLoopback(s)) return `127.0.0.1:${g.port}`;
    if (lan && (r === s || (isLocal(r) && isLocal(s)))) return lan;
    return this.publicAddress(g) || lan || (isIPv4(s) ? `${s}:${g.port}` : "");
  }
}
