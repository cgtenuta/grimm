// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 GR!MM and contributors
// web/installer.html's core against tools/fm1_install.py and against a simulated FM-1 behind a fake Web MIDI:
//   - the wire format and the package checks, byte for byte against the Python (pack7, response, request, identity,
//     product, logical image, package_damage), on build/grimm.fwsc when it is there;
//   - a whole install: handshake, step 1 (the running firmware reads the package, starts the loader, the port goes
//     away and comes back as the loader), step 2 (the loader reads all of it), restart, identity check; the image
//     the simulated loader received must be the logical image, byte for byte;
//   - the refusals: another model, another firmware's loader, a damaged or loader-less package, a cut-off write.
//   node web/test_installer.mjs
import { readFileSync, existsSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import assert from "node:assert/strict";

const html = readFileSync(new URL("./installer.html", import.meta.url), "utf8");
const core = html.split("/*CORE*/")[1].split("/*END CORE*/")[0];
const C = new Function(`${core}
  return { pack7, unpack7, response, parseRequest, parseIdentity, productOf, logicalImage, packageDamage, loadPackage,
           hasLoaderMark, crc16, Updater, InstallError, DELAY, modelOf, pairOutput, HS_QUERY, UPGRADE, FINISH_CHECK,
           FINISH_WRITE, LOADER_MARK };`)();
Object.assign(C.DELAY, { open: 0, start: 0.01, reply: 0, loader: 0.02, reboot: 0.02, retry: 0.02, poll: 0.05, hs: 0.1,
                         idle_check: 0.4, idle_write: 0.6, wait_loader: 1.0, wait_reboot: 1.0 });

const hex = (a) => Buffer.from(a).toString("hex");
const sha = (a) => createHash("sha256").update(a).digest("hex");
let checks = 0;
const ok = (c, m) => { assert.ok(c, m); checks++; };
const eq = (a, b, m) => { assert.deepEqual(a, b, m); checks++; };

// ---- against the Python ------------------------------------------------------------------------------------------
const py = (code) => execFileSync("python3", ["-I", "-c", `import sys; sys.path.insert(0, "tools")\nimport fm1_install as f\n${code}`],
                                  { encoding: "utf8" }).trim();
const bytesIn = Uint8Array.from({ length: 77 }, (_, i) => (i * 37 + 11) & 0xFF);
eq(hex(C.pack7(bytesIn)), py(`print(f.pack7(bytes((i*37+11)&255 for i in range(77))).hex())`), "pack7");
eq(hex(C.unpack7(C.pack7(bytesIn))), hex(bytesIn), "unpack7 inverts pack7");
const data = Uint8Array.from({ length: 40 }, (_, i) => i * 5 & 0xFF);
eq(hex(C.response(0x1234, data, 3)), py(`print(f.response(0x1234, bytes((i*5)&255 for i in range(40)), 3).hex())`), "response");
eq(hex(C.response(0xF0000000, Uint8Array.of(1, 2), 0)), py(`print(f.response(0xF0000000, bytes([1,2]), 0).hex())`), "response, high addr");

function request(addr, n, fl = 0) {                  // what the device sends
  const u = [0, 0x59, 0x30, 0, 0, 0, fl, addr & 255, (addr >>> 8) & 255, (addr >>> 16) & 255, (addr >>> 24) & 255, n & 255, (n >> 8) & 255, 0];
  u.push(~u.slice(6).reduce((a, b) => a + b, 0) & 255);
  return Uint8Array.from([0xF0, ...C.pack7(u), 0xF7]);
}
function identity(text) {
  const d = new Uint8Array(34); d.set([0, 0x59, 0x11, 0, 0, 0]);
  for (let i = 0; i < text.length; i++) d[6 + i] = text.charCodeAt(i);
  return Uint8Array.from([0xF0, ...C.pack7(d), 0xF7]);
}
{
  const r = C.parseRequest(request(0xE0000000, 0, 5));
  eq([r.fl, r.addr, r.n], [5, 0xE0000000, 0], "request round trip");
  eq(py(`print(f.parse_request(bytes.fromhex("${hex(request(0x1000, 512, 1))}")))`), "(1, 4096, 512)", "Python reads our test request");
  eq(C.parseRequest(Uint8Array.of(0xF0, 1, 2, 0xF7)), null, "short request refused");
  const id = C.parseIdentity(identity("FM-1_966"));
  eq([id.text, id.model, id.version, id.loader], ["FM-1_966", "FM-1", 966, false], "identity");
  eq(C.parseIdentity(identity("ota-FM-1_966")).loader, true, "loader identity");
}

// ---- a package: the real one when it is built, else a made one ----------------------------------------------------
function fakePackage(product, withLoader = true) {   // a small valid logical image wrapped as .fwsc
  const enc = (b) => { const o = Uint8Array.from(b); let k = 0xFFFF; for (let i = 0; i < o.length; i++) { o[i] ^= k & 255; k = ((k << 1) ^ (k & 0x8000 ? 0x1021 : 0)) & 0xFFFF; } return o; };
  const files = [["flash.bin", new Uint8Array(5000).map((_, i) => i * 7 & 255)],
                 ["ota.bin", new TextEncoder().encode((withLoader ? C.LOADER_MARK : "NOLOADER-ABCDEFG") + "x".repeat(3000))]];
  let off = 0x400; const ents = [], parts = [];
  for (const [name, d] of files) {
    off = (off + 0xFF) & ~0xFF;
    const e = new Uint8Array(0x50), dv = new DataView(e.buffer);
    dv.setUint16(4, C.crc16(d), true); dv.setUint32(8, off, true); dv.setUint32(12, d.length, true);
    e.set(new TextEncoder().encode(name), 0x40);
    ents.push(enc(e)); parts.push([off, d]); off += d.length;
  }
  const lst = new Uint8Array(0x50 * ents.length); ents.forEach((e, i) => lst.set(e, i * 0x50));
  const hdr = new Uint8Array(0x40), hv = new DataView(hdr.buffer);
  hv.setUint32(4, off, true); hv.setUint16(8, ents.length, true);
  hv.setUint16(2, C.crc16(lst), true);
  hv.setUint16(0, C.crc16(hdr.subarray(2)), true);
  const logical = new Uint8Array(off).fill(0xFF);
  logical.set(enc(hdr)); logical.set(lst, 0x40);
  for (const [o, d] of parts) logical.set(d, o);
  const raw = [];
  for (let i = 0; i < 20; i++) {
    raw.push(...logical.subarray(i * 0x2F, (i + 1) * 0x2F));
    raw.push(i < product.length ? (product.charCodeAt(i) + i + 1) & 255 : 0x7D);
  }
  raw.push(...logical.subarray(20 * 0x2F));
  return { raw: Uint8Array.from(raw), logical };
}

const real = "build/grimm.fwsc";
if (existsSync(real)) {
  const raw = new Uint8Array(readFileSync(real));
  eq(C.productOf(raw), py(`print(f.product_of(open("${real}","rb").read()))`), "product, real package");
  eq(sha(C.logicalImage(raw)), py(`import hashlib; print(hashlib.sha256(f.logical_image(open("${real}","rb").read())).hexdigest())`),
     "logical image, real package");
  eq(C.packageDamage(C.logicalImage(raw)), null, "real package is whole");
  const bad = C.logicalImage(raw); bad[0x9000] ^= 1;
  eq(C.packageDamage(bad), py(`d=bytearray(f.logical_image(open("${real}","rb").read())); d[0x9000]^=1; print(f.package_damage(bytes(d)))`),
     "damage found alike");
  ok(C.hasLoaderMark(raw), "real package has the Felucca loader");
  console.log(`real package: ${C.productOf(raw)}, ${raw.length} B: ok`);
}

// ---- a simulated FM-1 behind a fake Web MIDI -----------------------------------------------------------------------
class Port {
  constructor(name, id, type) { this.name = name; this.id = id; this.type = type; this.state = "connected"; this.onmidimessage = null; this.sent = []; }
  async open() {} close() {}
}
class FakeFM1 {
  /* mode "app": the running firmware; on UPGRADE it asks for the first parts, then FINISH_CHECK, drops off the bus and
     comes back as the loader. mode "ota": the loader; on UPGRADE it reads everything, FINISH_WRITE, drops off and comes
     back as the new firmware. */
  constructor(image, opts = {}) {
    this.image = image; this.opts = opts; this.mode = opts.loader ? "ota" : "app"; this.ident = opts.ident || (opts.loader ? "ota-FM-1_966" : "FM-1_900");
    this.written = null; this.log = []; this.gen = 0; this.access = this.makeAccess(); this.queue = []; this.wait = null;
    this.plug();
  }
  makeAccess() {
    const a = { inputs: new Map(), outputs: new Map(), onstatechange: null }; return a;
  }
  plug() {
    this.gen++;
    this.inp = new Port("FM-1", "in" + this.gen, "input");
    this.out = new Port("FM-1", "out" + this.gen, "output");
    this.out.send = (pkt) => this.fromHost(Uint8Array.from(pkt));
    this.access.inputs.set(this.inp.id, this.inp);
    this.access.outputs.set(this.out.id, this.out);
  }
  unplug() {
    this.inp.state = "disconnected"; this.out.state = "disconnected";
    this.access.inputs.delete(this.inp.id); this.access.outputs.delete(this.out.id);
  }
  emit(pkt) { setTimeout(() => this.inp.onmidimessage?.({ data: pkt }), 0); }
  fromHost(pkt) {
    if (pkt[1] === 0x00 && pkt[2] === 0x32 && pkt[3] === 0x45) { this.emit(identity(this.ident)); return; }   // handshake
    if (pkt.length === C.UPGRADE.length && pkt.every((b, i) => b === C.UPGRADE[i])) { this.log.push("UPGRADE " + this.mode); this.start(); return; }
    const u = C.unpack7(pkt.subarray(1, pkt.length - 1));
    if (u[0] === 0 && u[1] === 0x59 && u[2] === 0x30) {                // a response
      const n = (u[3] | u[4] << 8) - 8, addr = (u[7] | u[8] << 8 | u[9] << 16 | u[10] << 24) >>> 0;
      const resp = { addr, data: u.subarray(14, 14 + n) };
      if (this.wait) { const w = this.wait; this.wait = null; w(resp); } else this.queue.push(resp);
    }
  }
  next() { return this.queue.length ? Promise.resolve(this.queue.shift()) : new Promise((r) => { this.wait = r; }); }
  async ask(addr, n) { this.emit(request(addr, n)); return this.next(); }
  async start() {
    if (this.busy) return; this.busy = true;
    try {
      if (this.mode === "app") {
        const r1 = await this.ask(0, 0x40); const cnt = r1.data[8] | r1.data[9] << 8; ok(r1.data.length === 0x40, "app: header read");
        await this.ask(0x40, 0x50 * 2);
        await this.ask(0x400, 512);
        await this.ask(C.FINISH_CHECK, 0);
        this.log.push("check done");
        await new Promise((r) => setTimeout(r, 10)); this.unplug();
        await new Promise((r) => setTimeout(r, 60)); this.mode = "ota"; this.ident = "ota-FM-1_966"; this.plug();
      } else {
        const buf = new Uint8Array(this.image.length); let got = 0;
        for (let a = 0; a < this.image.length; a += 512) {
          const n = Math.min(512, this.image.length - a);
          if (this.opts.cutAt != null && a >= this.opts.cutAt) { this.unplug(); this.busy = false; return; }
          const r = await this.ask(a, n); buf.set(r.data, a); got += r.data.length;
        }
        this.written = buf;
        await this.ask(C.FINISH_WRITE, 0);
        this.log.push("write done");
        await new Promise((r) => setTimeout(r, 10)); this.unplug();
        await new Promise((r) => setTimeout(r, 60)); this.mode = "app"; this.ident = this.opts.after || "FM-1_966"; this.plug();
      }
    } finally { this.busy = false; }
  }
}
async function run(fm, product, image) {
  const up = new C.Updater(fm.access);
  const dev = await up.find();
  if (!dev) throw new C.InstallError("notfound", "no device");
  const steps = [];
  const out = await up.install(dev, image, product, (k, a) => steps.push(k));
  return { out, steps };
}
async function fails(p, code) {
  try { await p; } catch (e) { eq(e.code, code, `error code ${code} (${e.message})`); return e; }
  assert.fail(`expected ${code}`);
}

{
  const { raw, logical } = fakePackage("FM-1_966");
  const loaded = C.loadPackage(raw);
  eq([loaded.product, hex(loaded.image) === hex(logical)], ["FM-1_966", true], "made package loads");
  // a whole install
  const fm = new FakeFM1(logical);
  const { out, steps } = await run(fm, loaded.product, loaded.image);
  eq(out, "FM-1_966", "install ends on the new identity");
  eq(hex(fm.written), hex(logical), "the loader received the logical image, byte for byte");
  ok(["start", "check", "loader", "write", "reboot", "done"].every((s) => steps.includes(s)), "every step reported: " + [...new Set(steps)]);
  eq(fm.log, ["UPGRADE app", "check done", "UPGRADE ota", "write done"], "device saw the update in order");
  // a device already in update mode: only the write
  const fm2 = new FakeFM1(logical, { loader: true });
  const r2 = await run(fm2, loaded.product, loaded.image);
  eq([r2.out, hex(fm2.written) === hex(logical), fm2.log.join()], ["FM-1_966", true, "UPGRADE ota,write done"], "resumed write");
  // a different model
  await fails(run(new FakeFM1(logical, { ident: "OTHER_900" }), loaded.product, loaded.image), "model");
  // another firmware's loader
  await fails(run(new FakeFM1(logical, { loader: true, ident: "ota-FM-1_850" }), loaded.product, loaded.image), "foreign");
  // reports another identity afterwards
  await fails(run(new FakeFM1(logical, { after: "FM-1_900" }), loaded.product, loaded.image), "mismatch");
  // the write cut off
  await fails(run(new FakeFM1(logical, { loader: true, cutAt: 2048 }), loaded.product, loaded.image), "lost");
  // no device
  await fails(run({ access: { inputs: new Map(), outputs: new Map() } }, "FM-1_966", logical), "notfound");
}
{
  const { raw } = fakePackage("FM-1_966");
  const bad = Uint8Array.from(raw); bad[3000] ^= 0xFF;
  await fails(Promise.resolve().then(() => C.loadPackage(bad)), "badpkg");
  await fails(Promise.resolve().then(() => C.loadPackage(fakePackage("FM-1_966", false).raw)), "badpkg");
  await fails(Promise.resolve().then(() => C.loadPackage(new Uint8Array(100))), "badpkg");
  ok(C.loadPackage(fakePackage("FM-1_966", false).raw, true).product === "FM-1_966", "force overrides the loader check");
  eq(C.pairOutput("USB MIDI 2", ["USB MIDI 1"]), "USB MIDI 1", "single output pairs");
  eq(C.pairOutput("FM-1", ["FM-1", "Other"]), "FM-1", "same name pairs");
}
console.log(`installer: ${checks} checks ok`);
