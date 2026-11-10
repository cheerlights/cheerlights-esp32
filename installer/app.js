// The install page: talks to the firmware's USB console over Web Serial to set the board up.
// Every command and reply is documented in docs/configure.md; the page sends nothing else.

const $ = (id) => document.getElementById(id);
const FIND_COLOR = "303030"; // dim white, so a long strip stays within USB power
const LIVE_URL = "https://api.thingspeak.com/channels/1417/feeds/last.json";
const JOIN_TIMEOUT_MS = 90000; // two saved networks out of range take the board about 45 s to try
const JOIN_POLL_MS = 2000;

// One line-based conversation with the board's console.
class Board {
  constructor(port) {
    this.port = port;
    this.lines = [];
    this.waiters = [];
    this.closed = false;
  }

  async open() {
    await this.port.open({ baudRate: 115200 });
    // Keep the reset lines released, so opening the port does not restart the board.
    await this.port.setSignals({ dataTerminalReady: false, requestToSend: false }).catch(() => {});
    this.writer = this.port.writable.getWriter();
    this.readLoop();
  }

  async readLoop() {
    const decoder = new TextDecoder();
    let buffer = "";
    while (!this.closed && this.port.readable) {
      this.reader = this.port.readable.getReader();
      try {
        for (;;) {
          const { value, done } = await this.reader.read();
          if (done) break;
          buffer += decoder.decode(value, { stream: true });
          let at;
          while ((at = buffer.indexOf("\n")) >= 0) {
            const line = buffer.slice(0, at).replace(/\r$/, "");
            buffer = buffer.slice(at + 1);
            this.lines.push(line);
            this.waiters = this.waiters.filter((w) => !w(line));
          }
        }
      } catch {
        // The board restarted or was unplugged; the port may come back.
      } finally {
        this.reader.releaseLock();
      }
      if (!this.closed) await sleep(300);
    }
  }

  // Resolves with the first line after now that matches, or rejects after the timeout.
  waitFor(pattern, timeout = 3000) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.waiters = this.waiters.filter((w) => w !== check);
        // The pattern is for the developer console; people get a plain message.
        console.warn(`No answer from the board, waiting for ${pattern}`);
        reject(new Error("The board did not answer. Check the cable, then try again."));
      }, timeout);
      const check = (line) => {
        if (!pattern.test(line)) return false;
        clearTimeout(timer);
        resolve(line);
        return true;
      };
      this.waiters.push(check);
    });
  }

  // Commands are never logged or kept, because a Wi-Fi command carries a password.
  async send(command) {
    await this.writer.write(new TextEncoder().encode(command + "\n"));
  }

  async ask(command, pattern, timeout) {
    const answer = this.waitFor(pattern, timeout);
    await this.send(command);
    return answer;
  }

  // Lines the board prints in reply to one command, collected for a short while.
  async collect(command, ms = 400) {
    const from = this.lines.length;
    await this.send(command);
    await sleep(ms);
    return this.lines.slice(from);
  }

  async close() {
    this.closed = true;
    try { await this.reader?.cancel(); } catch {}
    try { this.writer?.releaseLock(); } catch {}
    try { await this.port.close(); } catch {}
  }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

const state = {
  board: null,
  about: null, // { chip, fw, name, setup, link, saved, wifi, ip, cheerlights }
  pins: [],
  pin: null,
  order: null,
  findIndex: -1,
  firstSeen: null,
  testing: false,
  forgetArmed: false,
  networks: [],
  heard: [], // the last scan: { name, rssi, open }
  joinRun: 0, // bumps to stop an earlier join wait
};

// What the console answers to each settings command, good or bad, so log lines in between are skipped.
const REPLY = /^(\d+ LEDs? on GPIO|Color order|Serpentine rows|An RGB LED|Common anode|Brightness cap|No button|Button on GPIO|GPIO \d+ cannot|A row cannot|The order is|Each of the three|The RGB LED needs|The T-Display's screen|A screen is only|Color name|Flip |The color over Wi-Fi|The color from a phone|The \S+ has no Wi-Fi|Usage|Not a command)/;

// Each board's setup, kept in this browser by its Bluetooth name, so a board erased by an install can be put back.
// The setup is the board's own export, which never has Wi-Fi networks in it.
const SAVED_PREFIX = "cheerlights-setup:";
const SETUP_OK = /^(\d+ LEDs? on GPIO|Color order|Serpentine rows|An RGB LED|Common anode|Brightness cap|No button|Button on GPIO|The T-Display's screen|Color name|Flip |The color over Wi-Fi|The color from a phone)/;

// Presets with a screen, which get the look choices.
const SCREEN_PRESETS = new Set(["tdisplay"]);

function savedSetup(name) {
  try {
    const saved = JSON.parse(localStorage.getItem(SAVED_PREFIX + name) || "null");
    return saved && typeof saved.commands === "string" && typeof saved.chip === "string" ? saved : null;
  } catch {
    return null;
  }
}

function keepSetup(name, setup) {
  try {
    if (setup) localStorage.setItem(SAVED_PREFIX + name, JSON.stringify(setup));
    else localStorage.removeItem(SAVED_PREFIX + name);
  } catch {
    // Without storage the page still works; it just cannot put a setup back later.
  }
}

// Asks a set-up board for its settings as commands and keeps them. Firmware before 0.4.0 has no export.
async function rememberSetup() {
  const board = state.board;
  const line = await board.ask("export", /^export |^Not a command/, 2000).catch(() => null);
  if (!line || !line.startsWith("export ") || line === "export none") return;
  const shown = await board.ask("show", /^In use: /, 2000).catch(() => "");
  keepSetup(state.about.name, {
    chip: state.about.chip,
    commands: line.slice("export ".length),
    description: shown.replace(/^In use: /, "").replace(/\.$/, ""),
    kept: new Date().toISOString(),
  });
}

const CHIP_NAMES = { esp32: "ESP32", esp32s3: "ESP32-S3", esp32c3: "ESP32-C3", esp32c5: "ESP32-C5", esp32c6: "ESP32-C6", esp32h2: "ESP32-H2" };

function setStatus(el, text, kind = "") {
  el.textContent = text;
  el.className = `status ${kind}`.trim();
}

function busy(button, on) {
  button.disabled = on;
  button.setAttribute("aria-busy", on ? "true" : "false");
}

function parseAbout(line) {
  const fields = Object.fromEntries(line.replace(/^about /, "").split(" ").map((kv) => kv.split("=")));
  return {
    chip: fields.chip,
    fw: fields.fw,
    name: fields.name,
    setup: fields.setup,
    link: fields.link, // absent before firmware 0.5.0, which is Bluetooth only
    saved: fields.saved === undefined ? undefined : Number(fields.saved),
    wifi: fields.wifi,
    ip: fields.ip,
    cheerlights: fields.cheerlights,
  };
}

// ---- How the light gets the color ----

// Firmware 0.5.0 and later take `link`; the ESP32-H2 has no Wi-Fi.
function linkSupported() {
  return state.about?.link !== undefined;
}

function wifiSupported() {
  return linkSupported() && state.about.chip !== "esp32h2";
}

function chosenLink() {
  if (!wifiSupported()) return "bluetooth";
  return document.querySelector('input[name="link"]:checked')?.value || "bluetooth";
}

function setLink(link) {
  const input = document.querySelector(`input[name="link"][value="${link === "wifi" ? "wifi" : "bluetooth"}"]`);
  if (input) input.checked = true;
}

function showLinkChoice() {
  const note = $("link-note");
  $("link-wifi").hidden = !wifiSupported();
  $("link-bluetooth").hidden = !linkSupported();
  if (!linkSupported()) {
    note.textContent = "This firmware gets the color over Bluetooth only. To use Wi-Fi, install the newest firmware in step 1, then connect again.";
  } else if (!wifiSupported()) {
    note.textContent = "The ESP32-H2 has no Wi-Fi, so it gets the color from a phone over Bluetooth.";
  }
  note.hidden = wifiSupported();
  setLink(wifiSupported() && state.about.link === "wifi" ? "wifi" : "bluetooth");
}

// Stages the chosen link, after any command that resets the staged settings (board <name>).
async function sendLink() {
  if (!linkSupported()) return;
  const reply = await state.board.ask(`link ${chosenLink()}`, REPLY, 2000);
  if (!/^The color (over Wi-Fi|from a phone)/.test(reply)) throw new Error(reply);
}

// The board restarts after a save; this waits for it to come back and report its settings.
async function waitForRestart(board) {
  const line = await board.waitFor(/^settings /, 15000);
  await board.waitFor(/^ready name=/, 10000).catch(() => {});
  return line;
}

async function connect() {
  const button = $("connect");
  busy(button, true);
  setStatus($("connect-status"), "Connecting");
  stopJoinWait();
  try {
    if (state.board) await state.board.close();
    // The installer already asked for this port, so reuse it when there is exactly one.
    const known = await navigator.serial.getPorts();
    const port = known.length === 1 ? known[0] : await navigator.serial.requestPort();
    const board = new Board(port);
    await board.open();
    state.board = board;
    let line = null;
    for (let tries = 0; tries < 5 && !line; tries++) {
      line = await board.ask("about", /^about chip=/, 1500).catch(() => null);
    }
    $("step-connect").classList.remove("next");
    if (!line) throw new Error("The board did not answer. Is the CheerLights firmware installed? Try step 1, then connect again.");
    state.about = parseAbout(line);
    const chip = CHIP_NAMES[state.about.chip] || state.about.chip;
    const setUp = state.about.setup === "saved";
    const saved = setUp ? null : savedSetup(state.about.name);
    const restorable = saved && saved.chip === state.about.chip ? saved : null;
    setStatus($("connect-status"),
      `Connected to ${state.about.name}, an ${chip} running firmware ${state.about.fw}. ${setUp ? "It is set up already; you can change it below." : restorable ? "It is not set up, but this browser kept a copy of its last setup." : "It is not set up yet."}`,
      "ok");
    await loadChoices();
    showLinkChoice();
    showRestore(restorable);
    $("step-setup").hidden = false;
    $("step-wifi").hidden = true;
    $("step-app").hidden = true;
    if (setUp) {
      await rememberSetup();
      await showDone();
    }
  } catch (e) {
    if (e.name === "NotFoundError") setStatus($("connect-status"), "No port chosen.");
    else setStatus($("connect-status"), e.message, "error");
  } finally {
    busy(button, false);
  }
}

function showRestore(saved) {
  $("restore-panel").hidden = !saved;
  if (!saved) return;
  $("restore-note").textContent = `Last time this browser saw ${state.about.name}, it had ${saved.description || saved.commands}.`;
  const link = saved.commands.match(/(?:^|; )link (wifi|bluetooth)(?:;|$)/);
  if (link && wifiSupported()) setLink(link[1]);
}

// Sends the kept commands to a board that lost its setup, then the link chosen above, then saves.
async function restore() {
  const saved = savedSetup(state.about.name);
  const button = $("restore");
  if (!saved) return;
  busy(button, true);
  setStatus($("setup-status"), "Putting its setup back");
  try {
    for (const command of saved.commands.split(";").map((c) => c.trim()).filter(Boolean)) {
      const reply = await state.board.ask(command, REPLY, 2000);
      if (!SETUP_OK.test(reply)) throw new Error(`${reply} Choose what is connected below instead.`);
    }
    await sendLink();
    await save();
    $("restore-panel").hidden = true;
  } catch (e) {
    await state.board.ask("cancel", /^Changes dropped/, 2000).catch(() => {});
    setStatus($("setup-status"), e.message, "error");
  } finally {
    busy(button, false);
  }
}

async function loadChoices() {
  const board = state.board;
  const boardLines = await board.collect("boards", 500);
  const presets = boardLines
    .map((l) => l.match(/^ {2}(\S+)\s+(.+?)( \(untested\))?$/))
    .filter(Boolean)
    .map((m) => ({ name: m[1], board: m[2], tested: !m[3] }));

  const list = $("preset-list");
  list.replaceChildren(...presets.map((p) => {
    const label = document.createElement("label");
    label.className = "choice";
    const input = document.createElement("input");
    input.type = "radio";
    input.name = "choice";
    input.value = `preset:${p.name}`;
    input.dataset.board = p.board;
    input.dataset.tested = p.tested;
    const text = document.createElement("span");
    const strong = document.createElement("strong");
    strong.textContent = p.board;
    if (!p.tested) {
      const note = document.createElement("span");
      note.className = "untested";
      note.textContent = " (not yet tested on this board)";
      strong.append(note);
    }
    text.append(strong);
    label.append(input, text);
    return label;
  }));

  const pinsLine = await board.ask("pins", /^pins/, 2000);
  state.pins = pinsLine.split(" ").slice(1).map(Number);
  fillPinSelect($("pin"), "GPIO");
  for (const id of ["red-pin", "green-pin", "blue-pin"]) fillPinSelect($(id));
  fillPinSelect($("button-pin"), null, "none", "No button");
}

function fillPinSelect(select, placeholder, firstValue = "", firstLabel = placeholder) {
  const options = [];
  if (firstLabel) {
    const o = document.createElement("option");
    o.value = firstValue;
    o.textContent = firstLabel;
    options.push(o);
  }
  for (const pin of state.pins) {
    const o = document.createElement("option");
    o.value = String(pin);
    o.textContent = `GPIO ${pin}`;
    options.push(o);
  }
  select.replaceChildren(...options);
}

function chosen() {
  return document.querySelector('input[name="choice"]:checked')?.value || "";
}

async function onChoice() {
  const value = chosen();
  await stopTest();
  $("preset-panel").hidden = !value.startsWith("preset:");
  $("pixels-panel").hidden = value !== "pixels";
  $("rgb-panel").hidden = value !== "rgb";
  $("common-panel").hidden = value !== "pixels" && value !== "rgb";
  if (value.startsWith("preset:")) {
    const input = document.querySelector('input[name="choice"]:checked');
    const screen = SCREEN_PRESETS.has(value.slice("preset:".length));
    $("look-panel").hidden = !screen;
    $("preset-note").textContent = input.dataset.tested === "true"
      ? `${input.dataset.board}: ${screen ? "the screen, brightness, and buttons" : "the LEDs, brightness, and button"} are already known.`
      : `${input.dataset.board}: set from the maker's documentation, but not yet tried on a real board. If the colors look wrong, choose "Something else" instead.`;
  }
  updateSaveState();
}

async function savePreset() {
  const name = chosen().slice("preset:".length);
  const button = $("save-preset");
  busy(button, true);
  setStatus($("setup-status"), "Saving");
  try {
    const reply = await state.board.ask(`board ${name}`, /^Preset |^No preset/, 2000);
    if (reply.startsWith("No preset")) throw new Error(reply);
    if (SCREEN_PRESETS.has(name)) {
      for (const command of [`label ${$("look-label").checked ? "on" : "off"}`, `flip ${$("look-flip").checked ? "on" : "off"}`]) {
        const look = await state.board.ask(command, REPLY, 2000);
        if (!SETUP_OK.test(look)) throw new Error(look);
      }
    }
    await sendLink();
    await save();
  } catch (e) {
    await state.board.ask("cancel", /^Changes dropped/, 2000).catch(() => {});
    setStatus($("setup-status"), e.message, "error");
  } finally {
    busy(button, false);
  }
}

// Sends save, waits for the restart, and checks the board kept the settings.
async function save() {
  const board = state.board;
  const reply = await board.ask("save", /^Saved|^Not saved|^Nothing to save/, 3000);
  if (reply.startsWith("Not saved")) throw new Error(reply);
  if (reply.startsWith("Saved")) {
    setStatus($("setup-status"), "Saved. The board is restarting.");
    const settings = await waitForRestart(board);
    if (!settings.includes("(saved)")) throw new Error(`The board did not keep the settings: ${settings}`);
  }
  const about = await board.ask("about", /^about chip=/, 4000);
  state.about = parseAbout(about);
  await rememberSetup();
  setStatus($("setup-status"), "The board is set up and ready.", "ok");
  await showDone();
}

// Finding the pin: light each free pin in turn until the person sees their LEDs.
async function test(pin, color) {
  const count = ledCount();
  const reply = await state.board.ask(`test ${pin} ${count} ${color}`, /^test pin=|cannot drive|is the button|Usage/, 2000);
  if (!reply.startsWith("test pin=")) throw new Error(reply);
  state.testing = true;
}

async function stopTest() {
  if (!state.board || !state.testing) return;
  state.testing = false;
  await state.board.ask("test off", /^test off/, 2000).catch(() => {});
}

function ledCount() {
  const n = Number($("count").value);
  return Number.isInteger(n) && n >= 1 && n <= 512 ? n : 8;
}

async function findStart() {
  state.pin = null;
  state.order = null;
  $("order-field").hidden = true;
  $("shape-field").hidden = true;
  state.findIndex = 0;
  await findShow();
}

async function findShow() {
  const pin = state.pins[state.findIndex];
  if (pin === undefined) {
    $("find-ask").hidden = true;
    await stopTest();
    setStatus($("pin-result"), "No pin lit them. Check the data wire, power, and ground, and that the strip's arrows point away from the board, then try again.", "error");
    return;
  }
  await test(pin, FIND_COLOR);
  $("find-ask").hidden = false;
  $("find-question").textContent = `Lighting GPIO ${pin} (${state.findIndex + 1} of ${state.pins.length}). Are your LEDs lit?`;
  setStatus($("pin-result"), "");
}

async function findAnswer(lit) {
  if (lit) {
    choosePin(state.pins[state.findIndex]);
  } else {
    state.findIndex += 1;
    await findShow();
  }
}

async function choosePin(pin) {
  state.pin = pin;
  $("pin").value = String(pin);
  $("find-ask").hidden = true;
  setStatus($("pin-result"), `Your LEDs are on GPIO ${pin}.`, "ok");
  $("order-field").hidden = false;
  await orderStart();
}

// Color order in two questions: the color shown for the first byte, then for the second.
async function orderStart() {
  state.order = null;
  state.firstSeen = null;
  setStatus($("order-result"), "");
  $("order-ask").hidden = false;
  $("order-again").hidden = true;
  $("order-question").textContent = "Showing the first test color. What color do your LEDs show?";
  await test(state.pin, "ff0000");
}

async function orderAnswer(seen) {
  if (state.firstSeen === null) {
    state.firstSeen = seen;
    $("order-question").textContent = "Showing the second test color. What color do they show now?";
    await test(state.pin, "00ff00");
    return;
  }
  if (seen === state.firstSeen) {
    setStatus($("order-result"), "That was the same color both times. Starting again with the first color.", "error");
    await orderStart();
    return;
  }
  const third = ["r", "g", "b"].find((c) => c !== state.firstSeen && c !== seen);
  state.order = state.firstSeen + seen + third;
  await test(state.pin, FIND_COLOR);
  $("order-ask").hidden = true;
  $("order-again").hidden = false;
  setStatus($("order-result"), `Color order ${state.order.toUpperCase()}.`, "ok");
  $("shape-field").hidden = false;
  updateSaveState();
}

function updateSaveState() {
  const value = chosen();
  let why = "";
  if (value === "pixels") {
    if (state.pin === null) why = "Find the pin first.";
    else if (!state.order) why = "Answer the color question first.";
    else if ($("is-grid").checked && !(Number($("width").value) >= 2 && Number($("width").value) <= ledCount())) why = "LEDs per row must be at least 2 and no more than the number of LEDs.";
  } else if (value === "rgb") {
    const pins = ["red-pin", "green-pin", "blue-pin"].map((id) => $(id).value);
    if (new Set(pins).size !== 3) why = "Pick three different pins.";
  }
  const button = $("button-pin").value;
  if (!why && button !== "none") {
    const used = value === "pixels" ? [String(state.pin)] : ["red-pin", "green-pin", "blue-pin"].map((id) => $(id).value);
    if (used.includes(button)) why = "The button needs a pin of its own.";
  }
  $("save-custom").disabled = Boolean(why);
  $("custom-why").textContent = why;
}

async function saveCustom() {
  const button = $("save-custom");
  busy(button, true);
  setStatus($("setup-status"), "Saving");
  const commands = [];
  if (chosen() === "pixels") {
    const width = $("is-grid").checked ? Number($("width").value) : 0;
    commands.push(`pixels ${state.pin} ${ledCount()} ${width}`, `order ${state.order}`, `serpentine ${$("serpentine").checked && width ? "on" : "off"}`);
  } else {
    commands.push(`rgb ${$("red-pin").value} ${$("green-pin").value} ${$("blue-pin").value}`, `anode ${$("anode").checked ? "on" : "off"}`);
  }
  commands.push(`max ${Math.min(255, Math.max(1, Number($("max").value) || 64))}`, `button ${$("button-pin").value}`);
  try {
    await stopTest();
    for (const command of commands) {
      const reply = await state.board.ask(command, REPLY, 2000);
      if (!SETUP_OK.test(reply)) throw new Error(reply);
    }
    await sendLink();
    await save();
  } catch (e) {
    setStatus($("setup-status"), e.message, "error");
  } finally {
    busy(button, false);
  }
}

// ---- Wi-Fi networks ----

// Why a network cannot go to the board, or "" when it can. The console reads a name and password in double quotes,
// one byte at a time, and drops control characters, so those cannot be sent.
function networkProblem(name, password) {
  const bytes = (s) => new TextEncoder().encode(s).length;
  if (name.length === 0) return "Type the network's name.";
  if (name.includes('"') || password.includes('"')) {
    return 'The board cannot take a double quote (") in a network name or password, so this network cannot be added here.';
  }
  // eslint-disable-next-line no-control-regex
  if (/[\x00-\x1f\x7f]/.test(name + password)) return "The name or password has a character the board cannot take, such as a tab.";
  if (bytes(name) > 32) return `A Wi-Fi network name is at most 32 bytes, and this one is ${bytes(name)}.`;
  if (password.length > 0 && (bytes(password) < 8 || bytes(password) > 63)) {
    return "A Wi-Fi password is 8 to 63 characters. Leave it empty for an open network.";
  }
  return "";
}

const WIFI_REPLY = /^(Saved "|Forgot "|All Wi-Fi networks forgotten|No saved network|No saved Wi-Fi networks|Saved Wi-Fi networks, newest first:|A Wi-Fi |Could not read that line|Usage: wifi|The \S+ has no Wi-Fi)/;
const SCAN_REPLY = /^(Networks in range, strongest first:|No Wi-Fi networks in range|The scan did not finish|The \S+ has no Wi-Fi)/;
// A scan takes the board 2 to 4 seconds, and up to about 8 while Bluetooth shares the radio. A command sent during
// one waits for it, so Wi-Fi commands wait this long too.
const SCAN_TIMEOUT_MS = 15000;

// The scan line: "Home" -48 dBm saved, "Cafe" -71 dBm open. A quote or backslash in a name comes with a backslash.
function parseScan(reply) {
  const found = [];
  for (const m of reply.matchAll(/"((?:[^"\\]|\\.)*)" (-?\d+) dBm((?: saved| open)*)/g)) {
    found.push({
      name: m[1].replace(/\\(.)/g, "$1"),
      rssi: Number(m[2]),
      saved: m[3].includes(" saved"),
      open: m[3].includes(" open"),
    });
  }
  return found;
}

// Four bars, as a phone shows Wi-Fi signal.
function signalLevel(rssi) {
  if (rssi >= -55) return [4, "strong signal"];
  if (rssi >= -67) return [3, "good signal"];
  if (rssi >= -78) return [2, "fair signal"];
  return [1, "weak signal"];
}

async function scanNetworks() {
  const button = $("scan-again");
  const status = $("scan-status");
  busy(button, true);
  status.textContent = "Scanning for networks. This takes a few seconds.";
  try {
    const reply = await state.board.ask("wifi scan", SCAN_REPLY, SCAN_TIMEOUT_MS);
    if (!/^(Networks in range|No Wi-Fi networks)/.test(reply)) throw new Error(reply);
    state.heard = parseScan(reply);
    showScan();
  } catch (e) {
    state.heard = [];
    $("scan-list").replaceChildren();
    status.textContent = `${e.message} You can still type the network's name below.`;
  } finally {
    busy(button, false);
  }
}

// Saved follows the board's list, so a network added since the scan is marked too.
function showScan() {
  const found = state.heard;
  const list = $("scan-list");
  const typed = $("wifi-name").value;
  list.replaceChildren(...found.map((n) => {
    const item = document.createElement("li");
    const pick = document.createElement("button");
    pick.type = "button";
    pick.setAttribute("aria-pressed", String(n.name === typed));
    const [level, words] = signalLevel(n.rssi);
    const bars = document.createElement("span");
    bars.className = "bars";
    bars.dataset.level = String(level);
    bars.title = `${words}, ${n.rssi} dBm`;
    bars.setAttribute("aria-hidden", "true");
    bars.append(...[1, 2, 3, 4].map(() => document.createElement("i")));
    const name = document.createElement("span");
    name.className = "name";
    name.textContent = n.name;
    const tags = [state.networks.includes(n.name) ? "saved" : "", n.open ? "open, no password" : ""].filter(Boolean).join(", ");
    const tag = document.createElement("span");
    tag.className = "tag";
    tag.textContent = tags;
    pick.setAttribute("aria-label", [n.name, words, tags].filter(Boolean).join(", "));
    pick.append(bars, name, tag);
    pick.addEventListener("click", () => pickNetwork(n));
    item.append(pick);
    return item;
  }));
  $("scan-status").textContent = found.length === 0
    ? "The board found no networks it can join. If yours is hidden, type its name below."
    : "Only networks this board can join are listed. Most ESP32 boards join only 2.4 GHz networks.";
}

function pickNetwork(n) {
  $("wifi-name").value = n.name;
  for (const b of $("scan-list").querySelectorAll("button")) {
    b.setAttribute("aria-pressed", String(b.querySelector(".name").textContent === n.name));
  }
  const password = $("wifi-password");
  password.value = "";
  setStatus($("wifi-status"), "");
  if (n.open) {
    $("wifi-add").focus();
  } else {
    password.focus();
  }
}

async function refreshNetworks() {
  const reply = await state.board.ask("wifi list", /^(No saved Wi-Fi networks|Saved Wi-Fi networks, newest first:)/, SCAN_TIMEOUT_MS);
  state.networks = [...reply.matchAll(/"([^"]*)"/g)].map((m) => m[1]);
  const list = $("wifi-list");
  list.replaceChildren(...state.networks.map((name) => {
    const item = document.createElement("li");
    const text = document.createElement("span");
    text.textContent = name;
    const forget = document.createElement("button");
    forget.textContent = "Forget";
    forget.setAttribute("aria-label", `Forget ${name}`);
    forget.addEventListener("click", () => forgetNetwork(name, forget));
    item.append(text, forget);
    return item;
  }));
  list.hidden = state.networks.length === 0;
  $("wifi-none").hidden = state.networks.length > 0;
  if (state.heard.length > 0) showScan();
}

async function addNetwork() {
  const nameInput = $("wifi-name");
  const passwordInput = $("wifi-password");
  const name = nameInput.value;
  const password = passwordInput.value;
  const why = networkProblem(name, password);
  if (why) {
    setStatus($("wifi-status"), why, "error");
    return;
  }
  const button = $("wifi-add");
  busy(button, true);
  setStatus($("wifi-status"), "Saving it on the board");
  try {
    const command = password ? `wifi "${name}" "${password}"` : `wifi "${name}"`;
    const reply = await state.board.ask(command, WIFI_REPLY, SCAN_TIMEOUT_MS);
    if (!reply.startsWith('Saved "')) throw new Error(reply);
    passwordInput.value = "";
    nameInput.value = "";
    $("wifi-show").checked = false;
    passwordInput.type = "password";
    setStatus($("wifi-status"), `Saved "${name}" on the board.`, "ok");
    for (const b of $("scan-list").querySelectorAll("button")) b.setAttribute("aria-pressed", "false");
    await refreshNetworks();
    startJoinWait();
  } catch (e) {
    setStatus($("wifi-status"), e.message, "error");
  } finally {
    busy(button, false);
  }
}

async function forgetNetwork(name, button) {
  busy(button, true);
  try {
    const reply = await state.board.ask(`wifi forget "${name}"`, WIFI_REPLY, SCAN_TIMEOUT_MS);
    if (!reply.startsWith('Forgot "')) throw new Error(reply);
    setStatus($("wifi-status"), `Forgot "${name}".`, "ok");
    await refreshNetworks();
    if (state.networks.length > 0) startJoinWait();
    else showNoNetworks();
  } catch (e) {
    setStatus($("wifi-status"), e.message, "error");
    busy(button, false);
  }
}

// ---- The last step ----

function stopJoinWait() {
  state.joinRun += 1;
}

// A Wi-Fi light with no network saved yet: the last step points back to step 4.
function showNoNetworks() {
  stopJoinWait();
  $("join-retry").hidden = true;
  $("join-facts").hidden = true;
  $("join-app").hidden = true;
  setStatus($("join-status"), "Add a network in step 4, and the light joins it right away.");
}

// Asks the board how it is doing until it is on Wi-Fi and has the CheerLights color, or the wait runs out.
async function startJoinWait() {
  const run = ++state.joinRun;
  $("join-retry").hidden = true;
  $("join-facts").hidden = true;
  $("join-app").hidden = true;
  setStatus($("join-status"), "Joining Wi-Fi.");
  const deadline = Date.now() + JOIN_TIMEOUT_MS;
  while (run === state.joinRun) {
    const line = await state.board.ask("about", /^about chip=/, 3000).catch(() => null);
    if (run !== state.joinRun) return;
    if (line) {
      const about = parseAbout(line);
      if (about.wifi === "connected" && about.cheerlights === "connected") {
        state.about = about;
        setStatus($("join-status"), "On Wi-Fi and showing the CheerLights color.", "ok");
        $("join-name").textContent = about.name;
        // CL-8F00 is cheerlights-8f00.local on the network (docs/wifi.md).
        const host = `cheerlights-${about.name.replace(/^CL-/i, "").toLowerCase()}.local`;
        const link = $("join-host");
        link.textContent = host;
        link.href = `http://${host}/`;
        const hasIp = Boolean(about.ip && about.ip !== "none");
        $("join-ip").textContent = hasIp ? about.ip : "";
        $("join-ip").href = hasIp ? `http://${about.ip}/` : "";
        $("join-ip-wrap").hidden = !hasIp;
        $("join-facts").hidden = false;
        $("join-app").hidden = false;
        return;
      }
      setStatus($("join-status"), about.wifi === "connected"
        ? `On Wi-Fi at ${about.ip}. Waiting for the CheerLights color.`
        : "Joining Wi-Fi.");
    }
    if (Date.now() >= deadline) {
      setStatus($("join-status"), "It has not joined after 90 seconds. Check the network name and password above, and that the network is 2.4 GHz (only an ESP32-C5 can also join 5 GHz). The board keeps trying on its own.", "error");
      $("join-retry").hidden = false;
      return;
    }
    await sleep(JOIN_POLL_MS);
  }
}

async function forget() {
  const button = $("forget");
  if (!state.forgetArmed) {
    state.forgetArmed = true;
    button.textContent = "Click again to forget: the light stays off until you set it up again";
    setTimeout(() => { state.forgetArmed = false; button.textContent = "Forget its settings"; }, 6000);
    return;
  }
  state.forgetArmed = false;
  button.textContent = "Forget its settings";
  busy(button, true);
  stopJoinWait();
  try {
    await state.board.ask("reset", /^Settings forgotten/, 3000);
    await waitForRestart(state.board);
    keepSetup(state.about.name, null);
    $("step-app").hidden = true;
    $("step-wifi").hidden = true;
    setStatus($("setup-status"), "Settings forgotten. Choose what is connected to set it up again.");
    state.about.setup = "none";
  } catch (e) {
    setStatus($("setup-status"), e.message, "error");
  } finally {
    busy(button, false);
  }
}

// Bluetooth: connect from the app. Wi-Fi: add networks, then watch it join.
async function showDone() {
  $("device-name").textContent = state.about.name;
  const wifi = state.about.link === "wifi";
  $("done-bluetooth").hidden = wifi;
  $("done-title").textContent = wifi ? "5. Watch it join" : "4. Connect from the app";
  $("step-wifi").hidden = !wifi;
  $("done-wifi").hidden = !wifi;
  $("step-app").hidden = false;
  if (wifi) {
    await refreshNetworks();
    if (state.networks.length > 0) startJoinWait();
    else showNoNetworks();
    scanNetworks();
  } else {
    stopJoinWait();
  }
  showLiveColor();
}

// The color the board will show: the live CheerLights color.
async function showLiveColor() {
  try {
    const r = await fetch(LIVE_URL);
    const last = await r.json();
    const name = (last.field1 || "").trim();
    const hex = (last.field2 || "").trim();
    if (!/^#[0-9a-f]{6}$/i.test(hex)) return;
    $("live-swatch").style.background = hex;
    $("live-text").textContent = `The CheerLights color is ${name} right now.`;
    $("live").hidden = false;
  } catch {
    // The page works without it.
  }
}

// After the installer closes, step 2 is the one thing left to do, and a board lights nothing until it is done.
function nextStep() {
  const step = $("step-connect");
  step.classList.add("next");
  setStatus($("connect-status"), "Next, click Connect. The board stays dark until it is set up.");
  step.scrollIntoView({ behavior: "smooth", block: "start" });
  $("connect").focus({ preventScroll: true });
}

function init() {
  if (!("serial" in navigator)) {
    $("browser-note").hidden = false;
    $("connect").disabled = true;
  }
  fetch("version.txt").then((r) => (r.ok ? r.text() : "")).then((v) => {
    if (v.trim()) $("version").textContent = v.trim();
  }).catch(() => {});

  // The installer holds the port while it runs; connect afterwards. Its dialog is added to the body, not to the
  // button, so its closed event is heard on the document.
  document.addEventListener("closed", (e) => {
    if (e.target?.localName === "ewt-install-dialog" && !state.board) nextStep();
  });

  $("connect").addEventListener("click", connect);
  $("choices").addEventListener("change", onChoice);
  $("save-preset").addEventListener("click", savePreset);
  $("restore").addEventListener("click", restore);
  $("find-start").addEventListener("click", findStart);
  $("find-yes").addEventListener("click", () => findAnswer(true));
  $("find-no").addEventListener("click", () => findAnswer(false));
  $("pin").addEventListener("change", (e) => { if (e.target.value !== "") choosePin(Number(e.target.value)); });
  for (const b of document.querySelectorAll("[data-seen]")) b.addEventListener("click", () => orderAnswer(b.dataset.seen));
  $("is-grid").addEventListener("change", (e) => { $("grid-fields").hidden = !e.target.checked; updateSaveState(); });
  for (const id of ["width", "count", "red-pin", "green-pin", "blue-pin", "button-pin"]) $(id).addEventListener("input", updateSaveState);
  $("order-again").addEventListener("click", orderStart);
  $("save-custom").addEventListener("click", saveCustom);
  $("forget").addEventListener("click", forget);
  $("wifi-add").addEventListener("click", addNetwork);
  $("scan-again").addEventListener("click", scanNetworks);
  $("wifi-name").addEventListener("input", () => {
    for (const b of $("scan-list").querySelectorAll("button")) {
      b.setAttribute("aria-pressed", String(b.querySelector(".name").textContent === $("wifi-name").value));
    }
  });
  for (const id of ["wifi-name", "wifi-password"]) {
    $(id).addEventListener("keydown", (e) => { if (e.key === "Enter") addNetwork(); });
  }
  $("wifi-show").addEventListener("change", (e) => { $("wifi-password").type = e.target.checked ? "text" : "password"; });
  $("join-retry").addEventListener("click", startJoinWait);
}

init();
