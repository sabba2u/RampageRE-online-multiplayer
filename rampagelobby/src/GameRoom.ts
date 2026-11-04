import { Room, updateLobby, matchMaker } from "colyseus";
import type { Games, PublicGame } from "./Games";

/** One listed RampageRE game, as a Colyseus room, so the built-in LobbyRoom ("lobby") lists every game live for
 *  JavaScript clients (the games page). The registry (Games) makes and closes these; a Colyseus client can't create
 *  one. Nobody plays in the room: the guest's game joins the host's over ENet. The room's id is the join code. */
export class GameRoom extends Room<any, PublicGame> {
  static byCode = new Map<string, GameRoom>();
  /** Codes the bridge is making a room for right now: anything else asking to create a game is refused. */
  static expected = new Set<string>();

  autoDispose = false;   // lives as long as the game is listed, watchers or not
  maxClients = 0;

  async onCreate(o: { game: PublicGame }) {
    if (!o?.game?.code || !GameRoom.expected.delete(o.game.code)) throw new Error("games are listed by RampageRE, not here");
    this.roomId = o.game.code;
    GameRoom.byCode.set(o.game.code, this);
    await this.setMetadata(o.game);
  }

  /** setMetadata, and tell the lobby: Colyseus only does that itself on create, join, leave and lock. */
  async update(g: PublicGame) {
    await this.setMetadata(g);
    updateLobby(this);
  }

  close() { this.disconnect(); }

  onDispose() {
    if (GameRoom.byCode.get(this.roomId) === this) GameRoom.byCode.delete(this.roomId);
  }
}

/** Keeps one GameRoom per listed game. Events for a code run in order: a heartbeat can arrive while its room is
 *  still being made. */
export function bridge(games: Games) {
  const queue = new Map<string, Promise<void>>();
  const run = (code: string, f: () => Promise<void> | void) => {
    const next = (queue.get(code) ?? Promise.resolve()).then(f)
      .catch(err => console.error(`lobby room ${code}: ${err?.message ?? err}`));
    queue.set(code, next);
    next.then(() => { if (queue.get(code) === next) queue.delete(code); });
  };
  games.listeners.push({
    add: g => run(g.code, async () => {
      GameRoom.expected.add(g.code);
      await matchMaker.createRoom("game", { game: g });
    }),
    update: g => run(g.code, () => GameRoom.byCode.get(g.code)?.update(g)),
    remove: code => run(code, () => GameRoom.byCode.get(code)?.close()),
  });
  /** Waits for the rooms to catch up (the tests). */
  return { settled: () => Promise.all([...queue.values()]).then(() => {}) };
}
