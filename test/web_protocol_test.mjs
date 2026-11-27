// Host test for docs/owl-protocol.js (the Web MIDI loader page).
// Run: node test/web_protocol_test.mjs [BUILD_DIR ...]
// Each BUILD_DIR holds an OwlProgram build: patch.bin and one <Name>.syx made from
// it (`make sysex`, tools/build_owl_patch.sh or owl_write.py --syx-out of
// acdc-workbench). The page packs patch.bin and must produce the same bytes.
import { readFileSync, readdirSync } from "node:fs";
import { join } from "node:path";
import * as P from "../docs/owl-protocol.js";

let fails = 0;
const check = (ok, what) => { if (!ok) { fails++; console.log("FAIL " + what); } else console.log("ok   " + what); };
const throws = (fn, what) => { try { fn(); check(false, what); } catch { check(true, what); } };
const same = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);

// CRC-32 check value
check(P.crc32(new TextEncoder().encode("123456789")) === 0xcbf43926, "crc32 check value");
// 7-bit packing round trip, including lengths that are not multiples of 7
for (const n of [0, 1, 6, 7, 8, 13, 14, 15, 210, 211]) {
  const d = Uint8Array.from({ length: n }, (_, i) => (i * 37 + 200) & 255);
  check(same(P.sysexToData(P.dataToSysex(d)), d), `pack round trip ${n} B`);
}
for (const v of [0, 1, 40, 0x7f, 0x80, 0xdeadbeef, 0xffffffff]) check(P.decodeInt(P.encodeInt(v)) === v, `int round trip ${v}`);
check(P.encodeInt(5).length === 5, "encoded int is 5 bytes");

// Only the allowed commands can be built
for (const c of [0x13, 0x14, 0x15, 0x16, 0x17, 0x7d, 0x7e, 0x03]) throws(() => P.sysex(c), `sysex 0x${c.toString(16)} refused`);
for (const v of [3, 4, 26, 0x7f]) throws(() => P.request(v), `request ${v} refused`);
for (const s of [0, -1, 41, 1.5, NaN]) {
  throws(() => P.storeMessage(s), `store slot ${s} refused`);
  throws(() => P.sendSlotMessage(s), `send slot ${s} refused`);
  throws(() => P.programChange(s), `program change ${s} refused`);
}
check(same(P.storeMessage(9), [0xf0, 0x7d, 0x52, 0x11, ...P.encodeInt(9), 0xf7]), "store message layout");
check(same(P.runMessage(), [0xf0, 0x7d, 0x52, 0x12, 0xf7]), "run message layout");
check(P.uploadMessages(new Uint8Array(1000)).every(m => m.length <= 240 + 4 + 5 + 1), "upload packets fit 250 bytes");

// A synthetic patch: ProgramHeader + payload
function fakePatch(size, link = 0x2000c000, name = "WebTest") {
  const d = Uint8Array.from({ length: size }, (_, i) => (i * 131 + 7) & 255);
  const dv = new DataView(d.buffer);
  dv.setUint32(0, P.PATCH_MAGIC, true);
  dv.setUint32(4, link, true);
  dv.setUint32(8, link + size + 1024, true);
  d.fill(0, 28, 52);
  for (let i = 0; i < name.length; i++) d[28 + i] = name.charCodeAt(i);
  return d;
}
const good = fakePatch(18452);
const info = P.describePatch(good);
check(info.name === "WebTest" && info.region === "patchram" && info.problems.length === 0, "synthetic patch accepted");
check(same(P.parseSyx(P.toSyx(good)).data, good), "syx round trip");
check(P.uploadMessages(good).length === 2 + Math.ceil(good.length / 210), "packet count");
check(P.describePatch(fakePatch(40000, 0x24000000)).platform === "OWL3", "plusram means OWL3");
check(P.describePatch(fakePatch(100 * 1024)).warnings.length === 1, "patchram > 80 KB warns (too big for OWL3)");
check(P.describePatch(fakePatch(600 * 1024, 0x24000000)).problems.length > 0, "too big refused");

// Broken files are refused
const bad = P.toSyx(good); bad[bad.length - 3] ^= 1;
throws(() => P.parseSyx(bad), "syx with a wrong CRC refused");
const msgs = P.uploadMessages(good);
throws(() => P.parseSyx(Uint8Array.from([...msgs[0], ...msgs[2]].flat())), "syx with a missing packet refused");
check(P.describePatch(new Uint8Array(100)).problems.length > 0, "non-patch refused");
check(P.describePatch(fakePatch(1000, 0x20008000)).problems.some(s => s.includes("link address")), "unknown link address refused");

// Real builds, byte-identical with FirmwareSender-style .syx
for (const dir of process.argv.slice(2)) {
  const bin = new Uint8Array(readFileSync(join(dir, "patch.bin")));
  const syxName = readdirSync(dir).find(f => f.endsWith(".syx"));
  if (!syxName) { check(false, `${dir}: no .syx`); continue; }
  const i = P.describePatch(bin);
  check(i.problems.length === 0, `${i.name}: loadable (${i.region})`);
  check(same(P.toSyx(bin), new Uint8Array(readFileSync(join(dir, syxName)))), `${i.name}: packets byte-identical with ${syxName}`);
  check(same(P.parseSyx(new Uint8Array(readFileSync(join(dir, syxName)))).data, bin), `${i.name}: ${syxName} decodes to patch.bin`);
}

console.log(fails ? `${fails} FAILED` : "all passed");
process.exit(fails ? 1 : 0);
