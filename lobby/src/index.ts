import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { makeServer } from "./server";

/** config.json in the folder it is started from (lobby/); every field is optional.
 *    port           the lobby's port (HTTP + Colyseus, TCP), default 19980; PORT overrides it
 *    publicAddress  this machine's address on the internet, for games hosted on this PC or its network that don't
 *                   know their own (no UPnP): what guests from the internet are given. Leave it "" on a VPS
 *    trustProxy     true behind nginx / Caddy: the client's address comes from X-Forwarded-For */
type Config = { port: number; publicAddress: string; trustProxy: boolean };

function loadConfig(file: string): Config {
  let raw: Partial<Config> = {};
  try { raw = JSON.parse(fs.readFileSync(file, "utf8")); }
  catch (e: any) { if (e?.code !== "ENOENT") throw new Error(`${file}: ${e.message}`); }
  return {
    port: Number(process.env.PORT ?? raw.port ?? 19980),
    publicAddress: String(process.env.RAMPAGE_LOBBY_PUBLIC ?? raw.publicAddress ?? ""),
    trustProxy: !!raw.trustProxy,
  };
}

async function main() {
  const config = loadConfig(process.env.RAMPAGE_LOBBY_CONFIG ?? path.join(process.cwd(), "config.json"));
  const { gameServer } = makeServer(config);
  await gameServer.listen(config.port, "0.0.0.0");
  console.log(`RampageRE lobby on port ${config.port} (TCP): the games page, GET /games.txt, Colyseus room "lobby"`);
  // Where people find it: this PC's own addresses (a lobby on the host's PC), so they can be read off the console.
  const lan = Object.values(os.networkInterfaces()).flat()
    .filter(i => i && i.family === "IPv4" && !i.internal).map(i => i!.address);
  console.log(`  this PC:      http://localhost:${config.port}/`);
  for (const a of lan) console.log(`  its network:  http://${a}:${config.port}/`);
  if (config.publicAddress) console.log(`  internet:     http://${config.publicAddress}:${config.port}/ (forward TCP ${config.port} to this PC)`);
}

main().catch(e => {
  const msg = e?.code === "EADDRINUSE" ? "That port is already in use: is another lobby running?" : e?.stack ?? e;
  console.error(`\nThe lobby could not start: ${msg}`);
  process.exit(1);
});
