// AC/DC patch loader: Web MIDI front end for owl-protocol.js.
// Sends only what owl-protocol.js allows: upload, run, store, slot download,
// read-only requests and program changes.
import * as P from "./owl-protocol.js";

const $ = s => document.querySelector(s);
const sleep = ms => new Promise(r => setTimeout(r, ms));
const esc = s => String(s).replace(/[&<>"']/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c]));
const kb = n => (n / 1024).toFixed(1) + " KB";
const PACE_MS = 10;     // pause between upload packets, as tools/owl_write.py

const S = {
  access: null, inp: null, out: null,
  fw: "", id: "", stats: "", names: new Map(), running: null, program: null,
  staged: null,            // { data, info, source }
  busy: false,
};

/* ───────── log ───────── */
function log(text, kind = "") {
  const li = document.createElement("li");
  li.className = kind;
  li.textContent = `${new Date().toLocaleTimeString()}  ${text}`;
  $("#log").prepend(li);
}

/* ───────── MIDI transport ───────── */
// Last line of defence: anything not built by owl-protocol.js is refused here.
function send(bytes) {
  const b = bytes[0];
  if (b === 0xf0) {
    if (bytes[1] !== P.MFR || bytes[2] !== P.OMNI || !P.ALLOWED_SYSEX.has(bytes[3])) throw new Error("refused to send this SysEx message");
  } else if ((b & 0xf0) === 0xb0) {
    if (bytes[1] !== P.CC_REQUEST || !P.ALLOWED_REQUESTS.has(bytes[2])) throw new Error("refused to send this CC");
  } else if ((b & 0xf0) !== 0xc0) {
    throw new Error("refused to send this MIDI message");
  }
  if (!S.out) throw new Error("not connected");
  S.out.send(bytes);
}

const listeners = new Set();
function onMidi(ev) {
  const m = ev.data;
  if (P.isOwlSysex(m) && m[3] === P.CMD.PROGRAM_ERROR) log("Device error: " + P.ascii(m.subarray(4, m.length - 1)), "err");
  for (const fn of [...listeners]) fn(m);
}
/* Collect incoming messages until done() returns true, or until nothing came
   for quietMs, or maxMs passed. Resolves with what arrived. */
function collect(start, { done = () => false, quietMs = 600, maxMs = 5000 } = {}) {
  return new Promise((resolve, reject) => {
    const got = [];
    const t0 = performance.now();
    let last = t0;
    const fn = m => { got.push(m); last = performance.now(); if (done(m, got)) finish(); };
    const iv = setInterval(() => {
      const now = performance.now();
      if (now - last > quietMs || now - t0 > maxMs) finish();
    }, 50);
    function finish() { clearInterval(iv); listeners.delete(fn); resolve(got); }
    listeners.add(fn);
    try { start(); } catch (e) { clearInterval(iv); listeners.delete(fn); reject(e); }
  });
}
const errorsIn = msgs => msgs.filter(m => P.isOwlSysex(m) && m[3] === P.CMD.PROGRAM_ERROR).map(m => P.ascii(m.subarray(4, m.length - 1)));

// The firmware answers name lists and downloads from one background task, a new
// request cancels the running one. So all device jobs run one after another.
async function job(label, fn) {
  if (S.busy) { log("Busy, please wait.", "warn"); return; }
  S.busy = true; render();
  try { return await fn(); }
  catch (e) { log(`${label} failed: ${e.message}`, "err"); }
  finally { S.busy = false; render(); }
}

/* ───────── device operations ───────── */
async function readInfo() {
  const msgs = await collect(() => { send(P.request(0x20)); send(P.request(0x21)); send(P.request(0x23)); }, { quietMs: 400, maxMs: 2000 });
  for (const m of msgs) {
    if (!P.isOwlSysex(m)) continue;
    const t = P.ascii(m.subarray(4, m.length - 1));
    if (m[3] === P.CMD.FW_VERSION) S.fw = t;
    if (m[3] === P.CMD.DEVICE_ID) S.id = t;
    if (m[3] === P.CMD.DEVICE_STATS) S.stats = t;
  }
}
async function readNames() {
  const isPc = m => (m[0] & 0xf0) === 0xc0;
  const msgs = await collect(() => send(P.request(0x01)), { done: isPc, quietMs: 1500, maxMs: 8000 });
  const names = new Map();
  let pc = null;
  for (const m of msgs) {
    if (P.isOwlSysex(m) && m[3] === P.CMD.PRESET_NAME) { const n = P.parseName(m.subarray(4, m.length - 1)); names.set(n.slot, n); }
    if (isPc(m)) pc = m[1];
  }
  if (pc == null) log("The name list did not end with a program change; it may be incomplete.", "warn");
  S.running = names.get(0) || null;
  names.delete(0);
  S.names = names;
  S.program = pc;
}
async function refresh() { await readInfo(); await readNames(); }

/* Download a stored slot (SysEx 0x18): the device streams it back as upload packets. */
function downloadSlot(slot, progress = () => {}) {
  return new Promise((resolve, reject) => {
    let size = null, crc = null, next = 0, len = 0, timer;
    const chunks = [];
    const end = (err, val) => { clearTimeout(timer); listeners.delete(fn); err ? reject(err) : resolve(val); };
    const kick = () => { clearTimeout(timer); timer = setTimeout(() => end(new Error("download timed out")), 4000); };
    const fn = m => {
      if (!P.isOwlSysex(m)) return;
      if (m[3] === P.CMD.PROGRAM_ERROR) return end(new Error(P.ascii(m.subarray(4, m.length - 1))));
      if (m[3] !== P.CMD.UPLOAD) return;
      const body = m.subarray(4, m.length - 1), idx = P.decodeInt(body), rest = body.subarray(5);
      if (idx !== next) return end(new Error(`packet ${idx} arrived, expected ${next}: data got lost`));
      next++; kick();
      if (idx === 0) { size = P.decodeInt(rest); return; }
      if (len < size) { const d = P.sysexToData(rest); chunks.push(d); len += d.length; progress(Math.min(1, len / size)); return; }
      crc = P.decodeInt(rest);
      const data = new Uint8Array(size);
      let o = 0;
      for (const c of chunks) { data.set(c.subarray(0, Math.min(c.length, size - o)), o); o += c.length; }
      if (len < size) return end(new Error("download incomplete"));
      if (crc !== P.crc32(data)) return end(new Error("checksum of the download does not match"));
      end(null, data);
    };
    listeners.add(fn);
    kick();
    try { send(P.sendSlotMessage(slot)); } catch (e) { end(e); }
  });
}

async function upload(data, progress) {
  const msgs = P.uploadMessages(data);
  const errs = [];
  const fn = m => { if (P.isOwlSysex(m) && m[3] === P.CMD.PROGRAM_ERROR) errs.push(P.ascii(m.subarray(4, m.length - 1))); };
  listeners.add(fn);
  try {
    for (let i = 0; i < msgs.length; i++) {
      send(msgs[i]);
      progress((i + 1) / msgs.length);
      await sleep(PACE_MS);
      if (errs.length) throw new Error("device reported: " + errs.join("; "));
    }
    await sleep(300);
    if (errs.length) throw new Error("device reported: " + errs.join("; "));
  } finally { listeners.delete(fn); }
  log(`Uploaded ${msgs.length} packets.`);
}

function saveFile(bytes, name) {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
  a.download = name;
  document.body.append(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 10000);
}
const safeName = s => (s || "patch").replace(/[^A-Za-z0-9._-]+/g, "_");

/* ───────── actions ───────── */
async function connect() {
  if (!navigator.requestMIDIAccess) { log("This browser has no Web MIDI. Use a Chromium-based browser (Chrome, Edge, Brave, Opera) on desktop.", "err"); return; }
  try { S.access = await navigator.requestMIDIAccess({ sysex: true }); }
  catch (e) { log(`MIDI access refused (${e.message}). Allow MIDI with SysEx for this page in the site settings and reload.`, "err"); return; }
  S.access.onstatechange = () => { fillPorts(); if (S.out && S.out.state === "disconnected") { detach(); log("Device disconnected.", "warn"); } };
  fillPorts();
  const outs = [...S.access.outputs.values()];
  const pick = outs.find(o => /OWL-ACDC/i.test(o.name)) || outs.find(o => /OWL/i.test(o.name));
  if (pick) { $("#port").value = pick.id; await attach(pick.id); }
  else log(outs.length ? "No OWL port found, pick one from the list." : "No MIDI ports. Connect the AC/DC over USB.", "warn");
}
function fillPorts() {
  const sel = $("#port");
  const outs = [...S.access.outputs.values()];
  sel.innerHTML = `<option value="">– choose a port –</option>` + outs.map(o => `<option value="${esc(o.id)}">${esc(o.name)}</option>`).join("");
  if (S.out) sel.value = S.out.id;
}
function detach() {
  if (S.inp) S.inp.onmidimessage = null;
  Object.assign(S, { inp: null, out: null, fw: "", id: "", stats: "", names: new Map(), running: null, program: null });
  render();
}
async function attach(id) {
  detach();
  const out = S.access.outputs.get(id);
  if (!out) return;
  const inp = [...S.access.inputs.values()].find(i => i.name === out.name);
  if (!inp) { log(`No input port named ${out.name}.`, "err"); return; }
  S.out = out; S.inp = inp;
  inp.onmidimessage = onMidi;
  log(`Connected to ${out.name}.`);
  await job("Reading the device", refresh);
  if (!S.fw) log("The port does not answer like an OWL device.", "warn");
  else log(`${S.fw}, ${S.names.size} stored patches.`);
}
const isAcdc = () => /^AC\/DC/i.test(S.fw);

async function activate(slot) {
  await job("Program change", async () => {
    send(P.programChange(slot));
    await sleep(800);
    await readNames();
    log(`Program change to slot ${slot}, now running: ${S.running?.name ?? "?"}. Not stored, a power cycle brings back the start-up patch.`);
  });
}
async function backupSlot(slot, kind) {
  const item = S.names.get(slot);
  await job("Download", async () => {
    const data = await downloadSlot(slot, f => setProgress(f));
    const ok = data.length === item.size && P.crc32(data) === item.crc;
    if (!ok) throw new Error("the download does not match the size and CRC the device lists");
    saveFile(kind === "syx" ? P.toSyx(data) : data, `slot${String(slot).padStart(2, "0")}_${safeName(item.name)}.${kind}`);
    log(`Downloaded slot ${slot} (${item.name}, ${data.length} B, CRC ${P.hex8(P.crc32(data))}).`, "ok");
  });
  setProgress(null);
}

function guardStaged() {
  const st = S.staged;
  if (!st) throw new Error("no patch chosen");
  if (st.info.problems.length) throw new Error(st.info.problems.join("; "));
  return st;
}

async function runStaged() {
  const st = guardStaged();
  if (!await confirmDevice(`Run ${st.info.name} from RAM?`, "The patch replaces the running one until the next power cycle or program change. Nothing is stored.", "Run")) return;
  await job("Run", async () => {
    await upload(st.data, f => setProgress(f));
    const msgs = await collect(() => send(P.runMessage()), { quietMs: 1000, maxMs: 5000 });
    const errs = errorsIn(msgs);
    if (errs.length) throw new Error(errs.join("; "));
    await readNames();
    const ok = S.running && S.running.name === st.info.name;
    log(ok ? `Running from RAM: ${S.running.name}.` : `Run sent, but the device reports ${S.running?.name ?? "nothing"} as running.`, ok ? "ok" : "warn");
  });
  setProgress(null);
}

async function storeStaged() {
  const st = guardStaged();
  const slot = +$("#slot").value;
  const old = S.names.get(slot);
  if (old && old.size === st.data.length && old.crc === st.info.crc) { log(`Slot ${slot} already holds this patch (same size and CRC). Nothing to do.`, "ok"); return; }
  const plan = await confirmStore(slot, old, st);
  if (!plan) return;
  await job("Store", async () => {
    if (old) {
      let copy = plan.copy;
      if (!copy) {
        log(`Downloading slot ${slot} (${old.name}) as a backup…`);
        copy = await downloadSlot(slot, f => setProgress(f));
        if (copy.length !== old.size || P.crc32(copy) !== old.crc) throw new Error("backup download does not match the slot. Nothing was written");
        saveFile(P.toSyx(copy), `slot${String(slot).padStart(2, "0")}_${safeName(old.name)}.syx`);
        log(`Backup of slot ${slot} offered as a file. Keep it before you go on.`, "ok");
      } else log(`Using your local copy of slot ${slot} as the backup (size and CRC match).`);
    }
    await upload(st.data, f => setProgress(f));
    const errs = errorsIn(await collect(() => send(P.storeMessage(slot)), { quietMs: 1000, maxMs: 4000 }));
    if (errs.length) throw new Error(errs.join("; "));
    const t0 = performance.now();
    let got;
    while (performance.now() - t0 < 30000) {
      await readNames();
      got = S.names.get(slot);
      if (got && got.size === st.data.length && got.crc === st.info.crc) break;
      await sleep(500);
    }
    if (!(got && got.size === st.data.length && got.crc === st.info.crc))
      throw new Error(`slot ${slot} reads back as ${got ? `${got.name}, ${got.size} B, CRC ${P.hex8(got.crc)}` : "empty"}`);
    log(`Stored in slot ${slot}: ${got.name}, ${got.size} B, CRC ${P.hex8(got.crc)}. The device now runs it.`, "ok");
  });
  setProgress(null);
}

/* ───────── dialogs ───────── */
// valid() decides whether OK is enabled; finalize() runs on OK and may return
// false to keep the dialog open, or a value to resolve with.
function dialog(title, html, okLabel, valid = () => true, finalize = async () => true) {
  const d = $("#dlg");
  $("#dlgTitle").textContent = title;
  $("#dlgBody").innerHTML = html;
  $("#dlgOk").textContent = okLabel;
  return new Promise(resolve => {
    const ok = $("#dlgOk"), cancel = $("#dlgCancel");
    const update = () => { ok.disabled = !valid(); };
    d.querySelectorAll("input").forEach(i => { i.addEventListener("input", update); i.addEventListener("change", update); });
    update();
    const close = v => { ok.onclick = cancel.onclick = null; d.close(); resolve(v); };
    ok.onclick = async () => { const v = await finalize(); if (v !== false) close(v); };
    cancel.onclick = () => close(null);
    d.oncancel = () => close(null);
    d.showModal();
  });
}
function deviceWarning() {
  if (isAcdc()) return "";
  return `<p class="warn">This page has only been tested with the AC/DC. Your device reports <b>${esc(S.fw || "no firmware name")}</b>.</p>
    <label><input type="checkbox" id="ackDev"> I understand, try it anyway</label>`;
}
const ackOk = () => !$("#ackDev") || $("#ackDev").checked;
function confirmDevice(title, text, okLabel) {
  return dialog(title, `<p>${esc(text)}</p>${deviceWarning()}`, okLabel, ackOk);
}
function confirmStore(slot, old, st) {
  const occupied = old ? `<p>Slot ${slot} holds <b>${esc(old.name)}</b> (${old.size} B, CRC ${P.hex8(old.crc)}). It will be downloaded first and offered as a .syx file. Only after a good download is the slot overwritten.</p>
    <p class="small">Or use your own copy of that slot instead of the download (.bin or .syx, checked against size and CRC):<br><input type="file" id="copyFile" accept=".bin,.syx"></p>` :
    `<p>Slot ${slot} is empty.</p>`;
  const slot1 = slot === 1 ? `<p class="warn">Slot 1 is the patch that starts at power-on when nothing else is chosen.</p><label><input type="checkbox" id="ackSlot1"> Yes, replace slot 1</label>` : "";
  return dialog(`Store ${st.info.name} in slot ${slot}?`, occupied + slot1 + deviceWarning() +
    `<p class="small">After storing, the device runs the patch from the slot.</p>`, "Store",
    () => ackOk() && (!$("#ackSlot1") || $("#ackSlot1").checked),
    async () => {
      const f = $("#copyFile")?.files?.[0];
      if (!f) return { copy: null };
      try {
        const { data } = P.loadFileBytes(new Uint8Array(await f.arrayBuffer()));
        if (data.length !== old.size || P.crc32(data) !== old.crc) { log(`${f.name} is not a copy of slot ${slot} (${data.length} B, CRC ${P.hex8(P.crc32(data))}).`, "err"); return false; }
        return { copy: data };
      } catch (e) { log(`${f.name}: ${e.message}`, "err"); return false; }
    });
}

/* ───────── staging ───────── */
function stage(data, source) {
  S.staged = { data, info: P.describePatch(data), source };
  render();
}
async function stageFile(file) {
  try {
    const { data, ignored, format } = P.loadFileBytes(new Uint8Array(await file.arrayBuffer()));
    stage(data, file.name);
    if (ignored) log(`${file.name}: ${ignored} non-upload message(s) in the file were ignored (only Run or Store from this page apply).`, "warn");
    log(`Loaded ${file.name} (${format}).`);
  } catch (e) { log(`${file.name}: ${e.message}`, "err"); }
}
/* ───────── rendering ───────── */
function setProgress(f) {
  const bar = $("#progress");
  bar.hidden = f == null;
  if (f != null) bar.value = f;
}
function render() {
  const conn = !!S.out;
  $("#devInfo").innerHTML = conn ? `
    <dt>Port</dt><dd>${esc(S.out.name)}</dd>
    <dt>Firmware</dt><dd>${esc(S.fw || "–")}</dd>
    <dt>Device ID</dt><dd class="mono">${esc(S.id || "–")}</dd>
    <dt>Storage</dt><dd>${esc(S.stats || "–")}</dd>
    <dt>Running</dt><dd>${S.running ? esc(S.running.name) + (S.program === 0 ? " (from RAM)" : S.program ? ` (slot ${S.program})` : "") : "–"}</dd>` :
    `<dt>Status</dt><dd>not connected</dd>`;
  const rows = [...S.names.values()].sort((a, b) => a.slot - b.slot);
  $("#slots").innerHTML = !conn ? `<tr><td colspan="5" class="empty">Connect to see the stored patches.</td></tr>` :
    rows.length ? rows.map(n => `<tr${n.slot === S.program ? ' class="cur"' : ""}>
      <td>${n.slot}</td><td>${esc(n.name)}${n.slot === S.program ? ' <span class="tag">running</span>' : ""}</td>
      <td class="num">${kb(n.size)}</td><td class="mono">${P.hex8(n.crc)}</td>
      <td class="acts"><button data-act="pc" data-slot="${n.slot}" ${S.busy ? "disabled" : ""}>Activate</button>
        <button data-act="syx" data-slot="${n.slot}" ${S.busy ? "disabled" : ""}>↓ .syx</button>
        <button data-act="bin" data-slot="${n.slot}" ${S.busy ? "disabled" : ""}>↓ .bin</button></td></tr>`).join("")
      : `<tr><td colspan="5" class="empty">No stored patches.</td></tr>`;

  const st = S.staged;
  $("#staged").hidden = !st;
  if (st) {
    const i = st.info;
    $("#stagedInfo").innerHTML = `
      <dt>Name</dt><dd><b>${esc(i.name || "?")}</b></dd>
      <dt>Source</dt><dd>${esc(st.source)}</dd>
      <dt>Size</dt><dd>${i.size} B</dd>
      <dt>CRC32</dt><dd class="mono">${P.hex8(i.crc)}</dd>
      <dt>Links to</dt><dd>${i.link != null ? `${P.hex8(i.link)} ${esc(i.region || "")}` : "–"} ${i.platform ? `(${esc(i.platform)})` : ""}</dd>
      ${i.warnings.map(w => `<dt>Note</dt><dd class="warn">${esc(w)}</dd>`).join("")}
      ${i.problems.map(w => `<dt>Problem</dt><dd class="err">${esc(w)}</dd>`).join("")}`;
    const sel = $("#slot"), prev = +sel.value;
    let opts = "";
    for (let s = 1; s <= P.MAX_SLOT; s++) { const n = S.names.get(s); opts += `<option value="${s}">${s} · ${n ? esc(n.name) : "empty"}</option>`; }
    sel.innerHTML = opts;
    let free = 2;
    while (S.names.has(free) && free < P.MAX_SLOT) free++;
    sel.value = prev && sel.dataset.user ? prev : free;
    const can = conn && !S.busy && !i.problems.length;
    $("#runBtn").disabled = $("#storeBtn").disabled = !can;
  }
  $("#refreshBtn").disabled = !conn || S.busy;
}

/* ───────── init ───────── */
function init() {
  $("#connectBtn").onclick = connect;
  $("#port").onchange = e => e.target.value && attach(e.target.value);
  $("#refreshBtn").onclick = () => job("Refresh", refresh);
  $("#slots").onclick = e => {
    const b = e.target.closest("button");
    if (!b) return;
    const slot = +b.dataset.slot;
    if (b.dataset.act === "pc") activate(slot);
    else backupSlot(slot, b.dataset.act);
  };
  $("#file").onchange = e => { if (e.target.files[0]) stageFile(e.target.files[0]); e.target.value = ""; };
  $("#slot").onchange = e => e.target.dataset.user = "1";
  $("#runBtn").onclick = () => runStaged().catch(e => log(e.message, "err"));
  $("#storeBtn").onclick = () => storeStaged().catch(e => log(e.message, "err"));
  if (!navigator.requestMIDIAccess) log("This browser has no Web MIDI. Use a Chromium-based browser (Chrome, Edge, Brave, Opera) on desktop.", "err");
  render();
}
init();
