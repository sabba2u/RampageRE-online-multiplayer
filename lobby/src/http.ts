import fs from "node:fs";
import { createRequire } from "node:module";
import type { IncomingMessage, ServerResponse } from "node:http";
import { Games, LobbyError, HEARTBEAT_S, isCode, type Game } from "./Games";
import { normIp } from "./addr";

/** The plain HTTP side, for RampageRE (src/netlobby.c) and anything else without a Colyseus SDK:
 *    POST   /games              register: name, monster, city, players, max, reserved?, port, build, external?, lan? (JSON or
 *                               form). Answers {code, token, heartbeat} as JSON, or "ok <code> <token> <heartbeat>"
 *                               when the request says Accept: text/plain (the game)
 *    PUT    /games/<code>       the heartbeat, every `heartbeat` seconds: token plus any field that changed. 404: the
 *                               lobby forgot it (restarted, or 30 s without one): register again
 *    DELETE /games/<code>       unlist: token in the body, ?token= or X-Token
 *    GET    /games              {games: [... with join, the address for whoever asked], you, heartbeat}
 *    GET    /games.txt          "rampage-lobby 1", then a line per game, tab-separated:
 *                               code  join  players  max  build  name  monster  city  reserved (the slots kept for
 *                               a player who dropped out: a ninth field older games' readers skip)
 *    GET    /games/<code>       one game, JSON; /games/<code>.txt the same as games.txt with only that line
 *  and for people:
 *    GET    /                   the games page (public/index.html): live list, rampage:// links, the join commands
 *    GET    /install.ps1        sets up rampage:// links on Windows, with this lobby's address filled in
 *    GET    /install-linux.sh   the same for Linux desktops (xdg-mime)
 *    GET    /health             "rampage_lobby" */
export function httpHandler(games: Games, opts: { trustProxy?: boolean } = {}) {
  return async (req: IncomingMessage, res: ServerResponse) => {
    const url = new URL(req.url ?? "/", "http://x");
    const send = (status: number, body: string, type = "text/plain; charset=utf-8") => {
      res.writeHead(status, { "Content-Type": type, "Access-Control-Allow-Origin": "*", "Cache-Control": "no-store" });
      res.end(body);
    };
    const json = (status: number, o: unknown) => send(status, JSON.stringify(o), "application/json");
    // Behind a reverse proxy (nginx, Caddy) every request comes from it: config.json's trustProxy takes the
    // client's address from X-Forwarded-For instead. Off by default, since anyone can send that header.
    const fwd = String(req.headers["x-forwarded-for"] ?? "").split(",")[0].trim();
    const from = normIp(opts.trustProxy && fwd ? fwd : req.socket.remoteAddress);
    const wantsText = String(req.headers.accept ?? "").includes("text/plain");
    try {
      if (req.method === "OPTIONS") {
        res.writeHead(204, { "Access-Control-Allow-Origin": "*", "Access-Control-Allow-Methods": "GET, POST, PUT, DELETE",
                             "Access-Control-Allow-Headers": "Content-Type, X-Token" });
        return res.end();
      }
      const p = url.pathname;
      if (req.method === "GET") {
        if (p === "/health") return send(200, "rampage_lobby\n");
        if (p === "/") return send(200, files.page(), "text/html; charset=utf-8");
        if (p === "/colyseus.js") return send(200, files.colyseus(), "text/javascript");
        if (p === "/install.ps1" || p === "/install-linux.sh") {
          const lobby = lobbyUrl(req);
          res.setHeader("Content-Disposition", `attachment; filename="${p === "/install.ps1" ? "install-rampage-links.ps1" : "install-rampage-links.sh"}"`);
          return send(200, p === "/install.ps1" ? files.windows(lobby) : files.linux(lobby));
        }
        if (p === "/games") return json(200, { games: games.list().map(g => view(games, g, from)), you: from, heartbeat: HEARTBEAT_S });
        if (p === "/games.txt") return send(200, text(games, games.list(), from));
        const m = p.match(/^\/games\/([A-Za-z0-9]+)(\.txt)?$/);
        if (m) {
          const g = isCode(m[1]) ? games.get(m[1]) : undefined;
          if (!g) return send(404, "error no game with that code\n");
          return m[2] ? send(200, text(games, [g], from)) : json(200, view(games, g, from));
        }
      }
      if (req.method === "POST" && p === "/games") {
        const g = games.register(await readBody(req), from);
        console.log(`listed ${g.code}: ${g.name} (${g.monster || "-"}, ${g.city || "menus"}) from ${from} port ${g.port}`);
        if (wantsText) return send(200, `ok ${g.code} ${g.token} ${HEARTBEAT_S}\n`);
        return json(200, { code: g.code, token: g.token, heartbeat: HEARTBEAT_S, address: games.publicAddress(g), lan: g.lan });
      }
      const m = p.match(/^\/games\/([A-Za-z0-9]+)$/);
      if (m && (req.method === "PUT" || req.method === "DELETE")) {
        const body = await readBody(req);
        const token = body.token ?? req.headers["x-token"] ?? url.searchParams.get("token");
        if (req.method === "PUT") games.heartbeat(m[1], { ...body, token });
        else {
          games.unregister(m[1], token);
          console.log(`unlisted ${m[1].toUpperCase()}`);
        }
        return wantsText ? send(200, "ok\n") : send(204, "");
      }
      send(404, "error not found\n");
    } catch (e: any) {
      send(e instanceof LobbyError ? e.status : 400, `error ${e?.message ?? e}\n`);
    }
  };
}

const view = (games: Games, g: Game, from: string) => ({ ...games.public(g), join: games.joinFor(g, from) });

const field = (s: string | number) => String(s).replace(/[\t\r\n]/g, " ");
function text(games: Games, list: Game[], from: string) {
  return ["rampage-lobby 1", ...list.map(g =>
    [g.code, games.joinFor(g, from) || "-", g.players, g.max, g.build, g.name, g.monster, g.city, g.reserved ?? 0].map(field)
      .join("\t"))]
    .join("\n") + "\n";
}

/** This lobby as the asker reached it ("http://host:port"), for the link installers; only a plain host name. */
function lobbyUrl(req: IncomingMessage) {
  const host = String(req.headers.host ?? "");
  const proto = String(req.headers["x-forwarded-proto"] ?? "") === "https" ? "https" : "http";
  return /^[A-Za-z0-9.\-]+(:\d{1,5})?$/.test(host) ? `${proto}://${host}` : "";
}

async function readBody(req: IncomingMessage): Promise<Record<string, unknown>> {
  let text = "";
  for await (const chunk of req) {
    text += chunk;
    if (text.length > 4096) throw new Error("too long");
  }
  if (!text.trim()) return {};
  if ((req.headers["content-type"] ?? "").includes("json")) {
    const o = JSON.parse(text);
    if (!o || typeof o !== "object" || Array.isArray(o)) throw new Error("expected a JSON object");
    return o;
  }
  return Object.fromEntries(new URLSearchParams(text));
}

/** The page, the Colyseus browser client and the installers, read once. */
const files = (() => {
  const root = new URL("../", import.meta.url);
  const read = (rel: string) => fs.readFileSync(new URL(rel, root), "utf8");
  let page: string | undefined, colyseus: string | undefined;
  const safe = (s: string) => s.replace(/['"`$\\\s]/g, "");
  return {
    page: () => (page ??= read("public/index.html")),
    // The package's exports don't include dist/: find it from the package's entry point.
    colyseus: () => (colyseus ??= fs.readFileSync(createRequire(import.meta.url).resolve("colyseus.js")
      .replace(/build[\\/]cjs[\\/]index\.js$/, "dist/colyseus.js"), "utf8")),
    // The handler goes inside the installer, so one download sets everything up.
    windows: (lobby: string) => read("launcher/install.ps1")
      .replace("__HANDLER__", () => read("launcher/rampage-handler.ps1").trimEnd())
      .replace("'__LOBBY__'", () => `'${safe(lobby)}'`),
    linux: (lobby: string) => read("launcher/install-linux.sh").replace("__LOBBY__", () => safe(lobby)),
  };
})();
