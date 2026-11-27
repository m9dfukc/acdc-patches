// OWL / OpenWare SysEx protocol, pure functions only (no DOM, no MIDI).
// Wire format from OpenWare: Source/OpenWareMidiControl.h, MidiHandler.cpp,
// FirmwareLoader.hpp, MidiController.cpp. Byte-identical with FirmwareSender and
// with tools/owl_write.py of acdc-workbench (checked by test/web_protocol_test.mjs).

export const MFR = 0x7d;          // MIDI_SYSEX_MANUFACTURER
export const OMNI = 0x52;         // MIDI_SYSEX_OMNI_DEVICE
export const CC_REQUEST = 67;     // REQUEST_SETTINGS

export const CMD = {
  PRESET_NAME: 0x01, RESOURCE_NAME: 0x04,
  UPLOAD: 0x10, STORE: 0x11, RUN: 0x12, ERASE: 0x14, SEND: 0x18,
  FW_VERSION: 0x20, DEVICE_ID: 0x21, PROGRAM_MESSAGE: 0x22,
  DEVICE_STATS: 0x23, PROGRAM_STATS: 0x24, PROGRAM_ERROR: 0x30,
};

// The only SysEx commands this page may send. Settings (0x15/0x16), firmware
// flash (0x13), resource save (0x17), reset (0x7d) and bootloader (0x7e) are
// deliberately missing. Erase (0x14) is allowed only through eraseMessage(),
// which always carries one patch slot: see there.
export const ALLOWED_SYSEX = new Set([CMD.UPLOAD, CMD.STORE, CMD.RUN, CMD.ERASE, CMD.SEND]);
// The only CC67 request values this page may send (all read-only).
export const ALLOWED_REQUESTS = new Set([0x01, 0x20, 0x21, 0x23, 0x24]);

// Patch slots 1..39. The firmware maps slot - 1 < MAX_NUMBER_OF_PATCHES - 1 (40) to
// patches, slot 40.. to resources (ProgramManager.cpp eraseFlashTask,
// MidiHandler::handleFirmwareSendCommand), so slot 40 would hit resource 0.
export const MAX_SLOT = 39;
export const DATA_PER_PACKET = 210;      // 240 encoded bytes, same as FirmwareSender
export const MAX_PROGRAM_SIZE = 144 * 1024;  // MAX_SYSEX_PROGRAM_SIZE (Source/device.h)
export const PATCH_MAGIC = 0xdadac0de;

// Patch load regions (Hardware/owl2.ld, owl3.ld). The header carries no platform
// field: 0x2000c000 is the patch RAM on OWL2 and OWL3 alike, only the size differs.
export const REGIONS = {
  0x2000c000: { name: "patchram", owl2: 144 * 1024, owl3: 80 * 1024 },
  0x24000000: { name: "plusram", owl2: 0, owl3: 512 * 1024 },
};

/* 8-bit <-> 7-bit packing: one byte with the high bits, then up to 7 low bytes */
export function dataToSysex(d) {
  const o = [];
  for (let i = 0; i < d.length; i += 7) {
    let m = 0;
    const n = Math.min(7, d.length - i);
    for (let j = 0; j < n; j++) m |= (d[i + j] >> 7) << j;
    o.push(m);
    for (let j = 0; j < n; j++) o.push(d[i + j] & 0x7f);
  }
  return o;
}
export function sysexToData(s) {
  const o = [];
  let bits = 0;
  for (let i = 0; i < s.length; i++) {
    if (i % 8 === 0) bits = s[i];
    else { o.push(s[i] | ((bits & 1) << 7)); bits >>= 1; }
  }
  return Uint8Array.from(o);
}
export const encodeInt = v => dataToSysex([(v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255]);
export function decodeInt(s) {
  const b = sysexToData(s.slice(0, 5));
  return ((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]) >>> 0;
}

const CRC_TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();
// Standard CRC-32 (zlib), which is what the device reports.
export function crc32(d) {
  let c = 0xffffffff;
  for (let i = 0; i < d.length; i++) c = CRC_TABLE[(c ^ d[i]) & 255] ^ (c >>> 8);
  return (c ^ 0xffffffff) >>> 0;
}
export const hex8 = n => "0x" + (n >>> 0).toString(16).padStart(8, "0");

export function sysex(cmd, payload = []) {
  if (!ALLOWED_SYSEX.has(cmd)) throw new Error(`SysEx command 0x${cmd.toString(16)} is not allowed`);
  if (cmd === CMD.ERASE && payload.length !== 5) throw new Error("erase needs exactly one slot number");
  return [0xf0, MFR, OMNI, cmd, ...payload, 0xf7];
}
export function request(value) {
  if (!ALLOWED_REQUESTS.has(value)) throw new Error(`request ${value} is not allowed`);
  return [0xb0, CC_REQUEST, value];
}
export function programChange(slot) {
  if (!Number.isInteger(slot) || slot < 1 || slot > MAX_SLOT) throw new Error(`bad slot ${slot}`);
  return [0xc0, slot];
}
export const runMessage = () => sysex(CMD.RUN);
export function storeMessage(slot) {
  if (!Number.isInteger(slot) || slot < 1 || slot > MAX_SLOT) throw new Error(`bad slot ${slot}`);
  return sysex(CMD.STORE, encodeInt(slot));
}
/* Erase one stored patch. MidiHandler::handleFlashEraseCommand: with no slot
   number the firmware erases the WHOLE storage, and eraseFromFlash() takes the
   slot as uint8_t, where 0xff also means "everything" and 0 reads out of bounds.
   So the slot is checked here, encoded, decoded again and must be 5 bytes. */
export function eraseMessage(slot) {
  if (!Number.isInteger(slot) || slot < 1 || slot > MAX_SLOT) throw new Error(`bad slot ${slot}`);
  const n = encodeInt(slot);
  if (n.length !== 5 || decodeInt(n) !== slot) throw new Error("internal error in the slot number");
  return sysex(CMD.ERASE, n);
}
export function isEraseMessage(m) {
  return m.length === 10 && m[0] === 0xf0 && m[3] === CMD.ERASE && m[9] === 0xf7 &&
    (() => { const s = decodeInt(m.slice(4, 9)); return s >= 1 && s <= MAX_SLOT; })();
}
export function sendSlotMessage(slot) {
  if (!Number.isInteger(slot) || slot < 1 || slot > MAX_SLOT) throw new Error(`bad slot ${slot}`);
  return sysex(CMD.SEND, encodeInt(slot));
}

/* Upload stream: packet 0 = size, data packets, last packet = CRC32 */
export function uploadMessages(data) {
  const msgs = [sysex(CMD.UPLOAD, [...encodeInt(0), ...encodeInt(data.length)])];
  let idx = 1;
  for (let off = 0; off < data.length; off += DATA_PER_PACKET)
    msgs.push(sysex(CMD.UPLOAD, [...encodeInt(idx++), ...dataToSysex(data.subarray(off, off + DATA_PER_PACKET))]));
  msgs.push(sysex(CMD.UPLOAD, [...encodeInt(idx), ...encodeInt(crc32(data))]));
  return msgs;
}
export const toSyx = data => Uint8Array.from(uploadMessages(data).flat());

export function splitSysex(bytes) {
  const out = [];
  let start = -1;
  for (let i = 0; i < bytes.length; i++) {
    if (bytes[i] === 0xf0) start = i;
    else if (bytes[i] === 0xf7 && start >= 0) { out.push(bytes.subarray(start, i + 1)); start = -1; }
  }
  return out;
}

/* .syx with upload packets -> patch binary. Other messages in the file
   (e.g. a trailing run or store command) are ignored and reported. */
export function parseSyx(bytes) {
  const msgs = splitSysex(bytes);
  let size = null, crc = null, next = 0, other = 0;
  const chunks = [];
  let len = 0;
  for (const m of msgs) {
    if (m.length < 5 || m[1] !== MFR || m[3] !== CMD.UPLOAD) { other++; continue; }
    const body = m.subarray(4, m.length - 1);
    const idx = decodeInt(body), rest = body.subarray(5);
    if (idx !== next) throw new Error(`packet ${idx} is out of sequence (expected ${next})`);
    next++;
    if (idx === 0) size = decodeInt(rest);
    else if (len < size) { const d = sysexToData(rest); chunks.push(d); len += d.length; }
    else crc = decodeInt(rest);
  }
  if (size == null) throw new Error("no upload packets (0x10) in the file");
  const data = new Uint8Array(size);
  let o = 0;
  for (const c of chunks) { data.set(c.subarray(0, Math.min(c.length, size - o)), o); o += c.length; }
  if (len < size) throw new Error(`file is incomplete: ${len} of ${size} bytes`);
  if (crc == null || crc !== crc32(data)) throw new Error("checksum in the file does not match the data");
  return { data, ignored: other };
}

export function loadFileBytes(bytes) {
  if (bytes[0] === 0xf0) return { ...parseSyx(bytes), format: "syx" };
  return { data: bytes, ignored: 0, format: "bin" };
}

/* ProgramHeader (Source/ProgramHeader.h): magic, link, end, jump, stack begin,
   stack end, program vector, 24-char name. */
export function describePatch(data) {
  const info = { size: data.length, crc: crc32(data), problems: [], warnings: [] };
  if (data.length < 52) { info.problems.push("too short for a patch"); return info; }
  const dv = new DataView(data.buffer, data.byteOffset, data.byteLength);
  const magic = dv.getUint32(0, true), link = dv.getUint32(4, true), end = dv.getUint32(8, true);
  let name = "";
  for (let i = 28; i < 52 && data[i]; i++) name += String.fromCharCode(data[i]);
  Object.assign(info, { name, link, programSize: (end - link) >>> 0 });
  if (magic !== PATCH_MAGIC) { info.problems.push(`magic is ${hex8(magic)}: not an OWL patch`); return info; }
  if (!name) info.problems.push("empty patch name");
  if (data.length > MAX_PROGRAM_SIZE) info.problems.push(`${data.length} B is more than the firmware accepts (${MAX_PROGRAM_SIZE} B)`);
  const r = REGIONS[link];
  if (!r) {
    info.problems.push(`link address ${hex8(link)} is no known patch region (0x2000c000 patchram, 0x24000000 plusram)`);
  } else {
    info.region = r.name;
    if (r.name === "plusram") {
      info.platform = "OWL3";
      if (info.programSize > r.owl3) info.problems.push(`program needs ${info.programSize} B, plusram has ${r.owl3} B`);
    } else {
      info.platform = info.programSize > r.owl3 ? "OWL2" : "OWL2 or OWL3";
      if (info.programSize > r.owl2) info.problems.push(`program needs ${info.programSize} B, patchram has at most ${r.owl2} B`);
      else if (info.programSize > r.owl3) info.warnings.push(`program needs ${info.programSize} B: too big for OWL3 devices such as the AC/DC (${r.owl3} B patchram)`);
    }
  }
  return info;
}

/* Incoming messages */
export const isOwlSysex = m => m.length > 4 && m[0] === 0xf0 && m[1] === MFR;
export function ascii(b) { let s = ""; for (const c of b) { if (!c) break; s += String.fromCharCode(c); } return s; }
// PRESET_NAME body: [slot, name..., 0, size(5), crc(5)]  (MidiController::sendName)
export function parseName(b) {
  let i = 1;
  while (i < b.length && b[i]) i++;
  return { slot: b[0], name: ascii(b.subarray(1, i)), size: decodeInt(b.subarray(i + 1, i + 6)), crc: decodeInt(b.subarray(i + 6, i + 11)) };
}
