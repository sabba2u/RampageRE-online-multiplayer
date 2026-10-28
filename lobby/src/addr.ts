/** IP addresses as the lobby sees them: where a request came from, and which address of a host a guest should use.
 *  Only IPv4 is handed out: ENet in RampageRE binds IPv4 (src/nettransport.c). */

/** IPv4 seen as IPv6 (::ffff:1.2.3.4) is that IPv4, and ::1 is 127.0.0.1. */
export function normIp(s: string | undefined): string {
  s = String(s ?? "").trim().toLowerCase().replace(/^::ffff:/, "");
  return s === "::1" ? "127.0.0.1" : s;
}

export function isIPv4(s: string): boolean {
  const m = s.match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/);
  return !!m && m.slice(1).every(o => Number(o) <= 255 && String(Number(o)) === o);
}

export const isLoopback = (s: string) => isIPv4(s) && s.startsWith("127.");

/** Addresses that only mean something on the network they are on: RFC 1918, link-local, and 100.64/10 (carrier-grade
 *  NAT, and Tailscale's), the last because two players on one tailnet reach each other there and nowhere else. */
export function isPrivate(s: string): boolean {
  if (!isIPv4(s)) return false;
  const [a, b] = s.split(".").map(Number);
  return a === 10 || (a === 172 && b >= 16 && b <= 31) || (a === 192 && b === 168) || (a === 169 && b === 254) ||
         (a === 100 && b >= 64 && b <= 127);
}

/** Somewhere only this network can reach: loopback or private. */
export const isLocal = (s: string) => isLoopback(s) || isPrivate(s);

/** A host name or IPv4 a player typed, or UPnP reported: letters, digits, dots and dashes, nothing else. */
export function isHostName(s: string): boolean {
  return s.length > 0 && s.length <= 64 && /^[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?$/.test(s) && !s.includes("..");
}

/** "host" or "host:port"; undefined when it is neither. */
export function splitHostPort(s: string): { host: string; port?: number } | undefined {
  const m = s.trim().match(/^([^:]+)(?::(\d{1,5}))?$/);
  if (!m || !isHostName(m[1])) return undefined;
  const port = m[2] === undefined ? undefined : Number(m[2]);
  if (port !== undefined && (port < 1 || port > 65535)) return undefined;
  return { host: m[1], port };
}
