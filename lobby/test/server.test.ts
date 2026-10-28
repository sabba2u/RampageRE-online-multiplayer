import { describe, it, expect, beforeAll, afterAll } from "vitest";
import { Client } from "colyseus.js";
import { makeServer } from "../src/server";

const port = 2611;
const base = `http://127.0.0.1:${port}`;
const until = async (check: () => boolean | Promise<boolean>, ms = 5000) => {
  for (const end = Date.now() + ms; Date.now() < end; await new Promise(r => setTimeout(r, 50)))
    if (await check()) return;
  throw new Error("timed out");
};
const form = (o: Record<string, string | number>) => new URLSearchParams(Object.entries(o).map(([k, v]) => [k, String(v)])).toString();

let made: ReturnType<typeof makeServer>;
beforeAll(async () => {
  made = makeServer({ publicAddress: "203.0.113.50" });
  await made.gameServer.listen(port);
});
afterAll(async () => made.gameServer.gracefullyShutdown(false));

describe("the lobby over HTTP", () => {
  it("registers as the game does (form body, text answer), lists, beats and unlists", async () => {
    const lobby = await new Client(`ws://127.0.0.1:${port}`).joinOrCreate("lobby");
    let listed: any[] = [];
    lobby.onMessage("rooms", rooms => { listed = rooms; });
    lobby.onMessage("+", ([id, room]) => { listed = [...listed.filter(r => r.roomId !== id), room]; });
    lobby.onMessage("-", id => { listed = listed.filter(r => r.roomId !== id); });

    const res = await fetch(`${base}/games`, { method: "POST", headers: { Accept: "text/plain", "Content-Type": "application/x-www-form-urlencoded" },
      body: form({ name: "sabba", monster: "George", city: "Chicago 1", players: 1, max: 2, port: 27980, build: "CAFE1234", lan: "192.168.1.20" }) });
    expect(res.status).toBe(200);
    const [ok, code, token, beat] = (await res.text()).trim().split(" ");
    expect([ok, code.length, token.length, beat]).toEqual(["ok", 5, 32, "10"]);

    // games.txt: the header, then the game with the address for this asker (the same PC: 127.0.0.1).
    const text = await (await fetch(`${base}/games.txt`)).text();
    expect(text.split("\n")[0]).toBe("rampage-lobby 1");
    expect(text).toContain(`${code}\t127.0.0.1:27980\t1\t2\tCAFE1234\tsabba\tGeorge\tChicago 1`);
    expect(await (await fetch(`${base}/games/${code.toLowerCase()}.txt`)).text()).toBe(`rampage-lobby 1\n${code}\t127.0.0.1:27980\t1\t2\tCAFE1234\tsabba\tGeorge\tChicago 1\n`);
    expect((await fetch(`${base}/games/ZZZZZ.txt`)).status).toBe(404);
    const one = await (await fetch(`${base}/games/${code}`)).json();
    expect(one).toMatchObject({ code, join: "127.0.0.1:27980", address: "203.0.113.50:27980", lan: "192.168.1.20:27980" });
    expect(one.token).toBeUndefined();
    await until(() => listed.some(r => r.roomId === code && r.metadata.name === "sabba"));

    // The heartbeat: another's token is refused; the host's updates the listing, live.
    expect((await fetch(`${base}/games/${code}`, { method: "PUT", body: form({ token: "x".repeat(32) }) })).status).toBe(403);
    const put = await fetch(`${base}/games/${code}`, { method: "PUT", headers: { "X-Token": token }, body: form({ players: 2, city: "Chicago 2" }) });
    expect(put.status).toBe(204);
    await until(() => listed.some(r => r.roomId === code && r.metadata.players === 2 && r.metadata.city === "Chicago 2"));

    expect((await fetch(`${base}/games/${code}?token=${token}`, { method: "DELETE" })).status).toBe(204);
    await until(() => !listed.some(r => r.roomId === code));
    expect((await (await fetch(`${base}/games`)).json()).games).toEqual([]);
    expect((await fetch(`${base}/games/${code}`, { method: "PUT", headers: { "X-Token": token } })).status).toBe(404);
    await lobby.leave();
  });

  it("refuses bad registrations with a reason", async () => {
    const res = await fetch(`${base}/games`, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ port: 1, build: "nothex!!" }) });
    expect(res.status).toBe(400);
    expect(await res.text()).toMatch(/^error build/);
    expect((await fetch(`${base}/games`, { method: "POST", headers: { "Content-Type": "application/json" }, body: "[1]" })).status).toBe(400);
  });

  it("serves the page, the browser client and both link installers", async () => {
    expect(await (await fetch(`${base}/`)).text()).toContain("<title>RampageRE Games</title>");
    expect(await (await fetch(`${base}/colyseus.js`)).text()).toContain("Colyseus");
    expect(await (await fetch(`${base}/health`)).text()).toBe("rampage_lobby\n");
    const win = await (await fetch(`${base}/install.ps1`)).text();
    expect(win).toContain(`[string]$Lobby = 'http://127.0.0.1:${port}'`);
    expect(win).toContain("rampage://join/");
    expect(win).not.toMatch(/^__HANDLER__$/m);
    expect(/^[\x00-\x7f]*$/.test(win)).toBe(true);   // Windows PowerShell reads scripts without a BOM as ANSI
    const linux = await (await fetch(`${base}/install-linux.sh`)).text();
    expect(linux).toContain(`lobby="\${2:-http://127.0.0.1:${port}}"`);
    expect(linux).toContain("x-scheme-handler/rampage");
  });

  it("refuses games created by a Colyseus client", async () => {
    await expect(new Client(`ws://127.0.0.1:${port}`).create("game", { game: { code: "ABCDE" } })).rejects.toThrow();
  });
});
