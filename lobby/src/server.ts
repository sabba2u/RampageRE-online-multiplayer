import http from "node:http";
import { Server, LobbyRoom } from "colyseus";
import { WebSocketTransport } from "@colyseus/ws-transport";
import { GameRoom, bridge } from "./GameRoom";
import { Games } from "./Games";
import { httpHandler } from "./http";

export type LobbyOptions = { publicAddress?: string; trustProxy?: boolean; now?: () => number };

/** The lobby: the HTTP list RampageRE uses, and Colyseus rooms ("lobby" lists every "game" live) for the page, on
 *  one port. It carries no game traffic: guests join hosts directly over ENet (UDP 27960). */
export function makeServer(opts: LobbyOptions = {}) {
  const games = new Games({ publicAddress: opts.publicAddress, now: opts.now });
  const rooms = bridge(games);
  const httpServer = http.createServer(httpHandler(games, { trustProxy: opts.trustProxy }));
  const gameServer = new Server({ transport: new WebSocketTransport({ server: httpServer }), greet: false });
  gameServer.define("lobby", LobbyRoom);
  gameServer.define("game", GameRoom).enableRealtimeListing();
  const sweeper = setInterval(() => games.sweep(), 5000);
  gameServer.onShutdown(() => clearInterval(sweeper));
  return { gameServer, games, rooms };
}
