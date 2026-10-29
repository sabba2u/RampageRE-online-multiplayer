import { describe, it, expect } from "vitest";
import { Games, parseFields, isCode, CODE_ALPHABET, CODE_LENGTH, EXPIRE_S, KNOCK_S, REGISTER_PER_MINUTE } from "../src/Games";
import { normIp, isPrivate, splitHostPort } from "../src/addr";

const base = { name: "sabba", monster: "George", city: "Chicago 1", players: 1, max: 2, port: 27960, build: "cafe1234" };

describe("addresses", () => {
  it("normalises and classifies", () => {
    expect(normIp("::ffff:192.168.1.5")).toBe("192.168.1.5");
    expect(normIp("::1")).toBe("127.0.0.1");
    expect(["10.0.0.1", "172.16.4.4", "192.168.0.9", "100.101.1.1"].every(isPrivate)).toBe(true);
    expect(["8.8.8.8", "172.32.0.1", "127.0.0.1", "1.2.3"].some(isPrivate)).toBe(false);
    expect(splitHostPort("example.org:27961")).toEqual({ host: "example.org", port: 27961 });
    expect(splitHostPort("a b")).toBeUndefined();
    expect(splitHostPort("1.2.3.4:99999")).toBeUndefined();
  });
});

describe("registration fields", () => {
  it("cleans names and checks numbers", () => {
    const f = parseFields({ ...base, name: "  evil\tname\n", build: "CAFE1234", players: "3", max: "2" });
    expect(f).toMatchObject({ name: "evil name", build: "CAFE1234", players: 2, max: 2 });
    expect(() => parseFields({ ...base, port: 70000 })).toThrow(/port/);
    expect(() => parseFields({ ...base, build: "xyz" })).toThrow(/build/);
    expect(() => parseFields({ ...base, lan: "8.8.8.8" })).toThrow(/lan/);
    expect(() => parseFields({ ...base, external: "a;b" })).toThrow(/external/);
    expect(parseFields({ ...base, name: "" }).name).toBe("Rampage host");
    expect(parseFields({ players: 2 }, true)).toEqual({ players: 2 });
  });
});

describe("the registry", () => {
  it("gives short unambiguous codes and checks the token", () => {
    const games = new Games();
    const g = games.register(base, "203.0.113.5");
    expect(g.code).toHaveLength(CODE_LENGTH);
    expect([...g.code].every(c => CODE_ALPHABET.includes(c))).toBe(true);
    expect(/[01OI]/.test(CODE_ALPHABET)).toBe(false);
    expect(isCode(g.code.toLowerCase())).toBe(true);
    expect(() => games.heartbeat(g.code, { token: "nope" })).toThrow(/not your game/);
    games.heartbeat(g.code.toLowerCase(), { token: g.token, players: 2, city: "Chicago 2" });
    expect(games.get(g.code)).toMatchObject({ players: 2, city: "Chicago 2" });
    // a guest dropped out: its slot kept for it, "1 / 2 (1 rejoining)"; never more than max with the players
    games.heartbeat(g.code, { token: g.token, players: 1, reserved: 1 });
    expect(games.get(g.code)).toMatchObject({ players: 1, max: 2, reserved: 1 });
    expect(games.public(games.get(g.code)!)).toMatchObject({ reserved: 1 });
    games.heartbeat(g.code, { token: g.token, players: 2 });
    expect(games.get(g.code)).toMatchObject({ players: 2, reserved: 0 });
    expect(() => games.heartbeat(g.code, { token: g.token, reserved: 9 })).toThrow(/reserved/);
    games.unregister(g.code, g.token);
    expect(games.list()).toHaveLength(0);
    expect(() => games.heartbeat(g.code, { token: g.token })).toThrow(/register again/);
  });

  it("forgets a game after EXPIRE_S without a heartbeat", () => {
    let t = 1000;
    const games = new Games({ now: () => t });
    const g = games.register(base, "203.0.113.5");
    t += (EXPIRE_S - 1) * 1000; games.sweep();
    expect(games.get(g.code)).toBeDefined();
    games.heartbeat(g.code, { token: g.token });
    t += (EXPIRE_S - 1) * 1000; games.sweep();
    expect(games.get(g.code)).toBeDefined();
    t += 2000; games.sweep();
    expect(games.get(g.code)).toBeUndefined();
  });

  it("rate-limits registrations per address, and a host starting again replaces its old listing", () => {
    let t = 0;
    const games = new Games({ now: () => t });
    games.register(base, "198.51.100.1");
    games.register(base, "198.51.100.1");
    expect(games.list()).toHaveLength(1);   // the same host and port: the old one went
    for (let i = 2; i < REGISTER_PER_MINUTE; i++) games.register({ ...base, port: 28000 + (i % 3) }, "198.51.100.1");
    expect(games.list()).toHaveLength(4);
    expect(() => games.register(base, "198.51.100.1")).toThrow(/too many/);
    games.register(base, "198.51.100.2");   // someone else is fine
    t += 61_000;
    expect(() => games.register({ ...base, port: 29001 }, "198.51.100.1")).toThrow(/at most/);   // still 4 listed
  });

  it("hands each guest the address that reaches the host from where it is", () => {
    const games = new Games();
    // A host on the internet, behind NAT, LAN address 192.168.1.20.
    const net = games.register({ ...base, lan: "192.168.1.20" }, "203.0.113.5");
    expect(games.joinFor(net, "198.51.100.9")).toBe("203.0.113.5:27960");   // from elsewhere: its public address
    expect(games.joinFor(net, "203.0.113.5")).toBe("192.168.1.20:27960");   // from behind the same NAT: its LAN one
    // UPnP told the host its address: that one goes out.
    const upnp = games.register({ ...base, port: 27961, external: "203.0.113.77" }, "203.0.113.6");
    expect(games.joinFor(upnp, "198.51.100.9")).toBe("203.0.113.77:27961");

    // The lobby on the host's PC: the host registers from 127.0.0.1 and says its LAN address.
    const local = new Games({ publicAddress: "203.0.113.50" });
    const h = local.register({ ...base, lan: "192.168.1.20" }, "127.0.0.1");
    expect(local.joinFor(h, "::1")).toBe("127.0.0.1:27960");                 // a second copy on the same PC
    expect(local.joinFor(h, "::ffff:192.168.1.30")).toBe("192.168.1.20:27960");   // the LAN
    expect(local.joinFor(h, "198.51.100.9")).toBe("203.0.113.50:27960");     // the internet: config's publicAddress
    expect(local.public(h)).toMatchObject({ address: "203.0.113.50:27960", lan: "192.168.1.20:27960" });
    // ...and without a publicAddress or UPnP, the internet gets the LAN address (it works over a VPN or tailnet).
    const bare = new Games();
    const b = bare.register({ ...base, lan: "100.100.1.2" }, "127.0.0.1");
    expect(bare.joinFor(b, "198.51.100.9")).toBe("100.100.1.2:27960");
    expect(bare.public(b).address).toBe("");
  });

  it("lists the host's STUN address and hands its guests' knocks to it once", () => {
    let t = 1_000_000;
    const games = new Games({ now: () => t });
    // STUN saw the host's socket as 203.0.113.5:61022 (the router's own port, not 27960): guests join that.
    const g = games.register({ ...base, stun: "203.0.113.5:61022", lan: "192.168.1.20" }, "203.0.113.5");
    expect(games.joinFor(g, "198.51.100.9")).toBe("203.0.113.5:61022");
    expect(() => games.register({ ...base, port: 27961, stun: "192.168.1.20:5000" }, "203.0.113.5")).toThrow(/stun/);
    // A guest with a STUN answer, and one without (its asking address plus its game's port).
    expect(games.knock(g.code, { stun: "198.51.100.9:50000" }, "198.51.100.9")).toBe("198.51.100.9:50000");
    expect(games.knock(g.code.toLowerCase(), { port: "51000" }, "::ffff:198.51.100.10")).toBe("198.51.100.10:51000");
    expect(() => games.knock(g.code, {}, "198.51.100.11")).toThrow();
    expect(() => games.knock("ZZZZZ", { port: 1 }, "198.51.100.11")).toThrow(/no game/);
    expect(() => games.takeKnocks(g.code, "wrong")).toThrow(/not your game/);
    expect(games.takeKnocks(g.code, g.token)).toEqual(["198.51.100.9:50000", "198.51.100.10:51000"]);
    expect(games.takeKnocks(g.code, g.token)).toEqual([]);   // collected: gone
    games.knock(g.code, { port: 52000 }, "198.51.100.12");
    t += (KNOCK_S + 1) * 1000;                                // too old: the guest gave up meanwhile
    expect(games.takeKnocks(g.code, g.token)).toEqual([]);
  });
});
