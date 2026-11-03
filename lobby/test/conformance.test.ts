import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { spawn, type ChildProcess } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { makeServer } from "../src/server";

/** One set of HTTP cases, everything the game sees, run against both lobbies: this one (Node) and the one built into
 *  the launcher (launcher/, Rust; `cargo build` it first, else its half is skipped). Each answer is checked exactly,
 *  so the two can't drift apart. */
const rustExe = path.resolve(__dirname, "../../launcher/target/debug/rampage-launcher.exe");
const hasRust = fs.existsSync(rustExe);
if (!hasRust) console.warn(`conformance: ${rustExe} isn't built (cargo build --manifest-path launcher/Cargo.toml): the Rust lobby is skipped`);

const form = (o: Record<string, string | number>) => new URLSearchParams(Object.entries(o).map(([k, v]) => [k, String(v)])).toString();
const GAME = { name: "sabba", monster: "George", city: "Chicago 1", players: 1, max: 2, port: 27960, build: "cafe1234" };
const FORM = { "Content-Type": "application/x-www-form-urlencoded" };

type Lobby = { name: string; port: number; start(): Promise<() => Promise<void>> };
const lobbies: Lobby[] = [
  { name: "Node", port: 2621, async start() {
      const made = makeServer();
      await made.gameServer.listen(this.port);
      return () => made.gameServer.gracefullyShutdown(false);
    } },
  { name: "Rust", port: 2622, async start() {
      const p: ChildProcess = spawn(rustExe, ["--serve-lobby", String(this.port)], { stdio: "ignore" });
      for (let i = 0; i < 100; i++) {
        try { if ((await fetch(`http://127.0.0.1:${this.port}/health`)).ok) break; } catch { /* not up yet */ }
        await new Promise(r => setTimeout(r, 50));
      }
      return async () => { p.kill(); };
    } },
];

describe.each(lobbies)("the $name lobby", lobby => {
  const base = `http://127.0.0.1:${lobby.port}`;
  let stop: () => Promise<void>;
  const run = lobby.name === "Node" || hasRust;
  beforeAll(async () => { if (run) stop = await lobby.start(); });
  afterAll(async () => { if (stop) await stop(); });

  const register = async (port = 27960) => {
    const res = await fetch(`${base}/games`, { method: "POST", headers: { Accept: "text/plain", ...FORM }, body: form({ ...GAME, port }) });
    expect(res.status).toBe(200);
    const [ok, code, token, beat] = (await res.text()).trim().split(" ");
    expect([ok, code.length, token.length, beat]).toEqual(["ok", 5, 32, "10"]);
    return { code, token };
  };

  it.runIf(run)("registers as text and as JSON", async () => {
    await register(27970);
    const res = await fetch(`${base}/games`, { method: "POST", headers: FORM, body: form({ ...GAME, port: 27971 }) });
    const v = await res.json();
    expect([res.status, v.heartbeat, v.code.length, v.token.length]).toEqual([200, 10, 5, 32]);
  });

  it.runIf(run)("lists in games.txt, /games and /games/<code>", async () => {
    const { code } = await register(27972);
    const line = `${code}\t127.0.0.1:27972\t1\t2\tCAFE1234\tsabba\tGeorge\tChicago 1\t0`;
    const all = await (await fetch(`${base}/games.txt`)).text();
    expect(all.split("\n")[0]).toBe("rampage-lobby 1");
    expect(all).toContain(line);
    expect(await (await fetch(`${base}/games/${code.toLowerCase()}.txt`)).text()).toBe(`rampage-lobby 1\n${line}\n`);
    const one = await (await fetch(`${base}/games/${code}`)).json();
    expect(one).toMatchObject({ code, name: "sabba", players: 1, max: 2, reserved: 0, port: 27972, build: "CAFE1234",
                                address: "", lan: "", join: "127.0.0.1:27972" });
    const list = await (await fetch(`${base}/games`)).json();
    expect([list.you, list.heartbeat]).toEqual(["127.0.0.1", 10]);
    const missing = await fetch(`${base}/games/ZZZZZ.txt`);
    expect([missing.status, await missing.text()]).toEqual([404, "error no game with that code\n"]);
  });

  it.runIf(run)("beats, refuses a wrong token, unlists", async () => {
    const { code, token } = await register(27973);
    const at = `${base}/games/${code}`;
    expect((await fetch(at, { method: "PUT", headers: FORM, body: form({ token, players: 2 }) })).status).toBe(204);
    const text = await fetch(at, { method: "PUT", headers: { Accept: "text/plain", ...FORM }, body: form({ token }) });
    expect([text.status, await text.text()]).toEqual([200, "ok\n"]);
    const wrong = await fetch(at, { method: "PUT", headers: FORM, body: form({ token: "nope" }) });
    expect([wrong.status, await wrong.text()]).toEqual([403, "error not your game\n"]);
    expect((await fetch(`${at}?token=${token}`, { method: "DELETE" })).status).toBe(204);
    const gone = await fetch(at, { method: "PUT", headers: FORM, body: form({ token }) });
    expect([gone.status, await gone.text()]).toEqual([404, "error no such game (it expired: register again)\n"]);
  });

  it.runIf(run)("passes a knock to the host once", async () => {
    const { code, token } = await register(27974);
    const k = await fetch(`${base}/games/${code}/knock`, { method: "POST", headers: FORM, body: form({ port: 51000 }) });
    expect([k.status, await k.text()]).toEqual([200, "ok\n"]);
    expect(await (await fetch(`${base}/games/${code}/knocks?token=${token}`)).text()).toBe("ok\nknock 127.0.0.1:51000\n");
    expect(await (await fetch(`${base}/games/${code}/knocks?token=${token}`)).text()).toBe("ok\n");

    // the players the game says with its knocks, kept for the guests' launchers (same game: registering is limited)
    const rows = "0\thost\t\t0\tGeorge\n1\tonline\t203.0.113.9:27960\t42\tLizzie";
    const kp = await fetch(`${base}/games/${code}/knocks?token=${token}&${form({ players: rows })}`);
    expect(kp.status).toBe(200);
    expect(await (await fetch(`${base}/games/${code}/players`)).json()).toEqual({ players: [
      { slot: 0, state: "host", name: "George", ping: 0 }, { slot: 1, state: "online", name: "Lizzie", ping: 42 }] });
    expect((await fetch(`${base}/games/ZZZZZ/players`)).status).toBe(404);
  });

  it.runIf(run)("answers health and unknown paths", async () => {
    expect(await (await fetch(`${base}/health`)).text()).toBe("rampage_lobby\n");
    const nope = await fetch(`${base}/nope`);
    expect([nope.status, await nope.text()]).toEqual([404, "error not found\n"]);
  });
});
