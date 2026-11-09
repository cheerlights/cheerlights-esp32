// The install page: talks to the firmware's USB console over Web Serial to set the board up.
// Every command and reply is documented in docs/configure.md; the page sends nothing else.

const $ = (id) => document.getElementById(id);
const FIND_COLOR = "303030"; // dim white, so a long strip stays within USB power
const LIVE_URL = "https://api.thingspeak.com/channels/1417/feeds/last.json";

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
        reject(new Error(`No answer from the board (waiting for ${pattern})`));
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
  about: null, // { chip, fw, name, setup }
  pins: [],
  pin: null,
  order: null,
  findIndex: -1,
  firstSeen: null,
  testing: false,
  forgetArmed: false,
};

// What the console answers to each settings command, good or bad, so log lines in between are skipped.
const REPLY = /^(\d+ LEDs? on GPIO|Color order|Serpentine rows|An RGB LED|Common anode|Brightness cap|No button|Button on GPIO|GPIO \d+ cannot|A row cannot|The order is|Each of the three|The RGB LED needs|Usage|Not a command)/;

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
  return { chip: fields.chip, fw: fields.fw, name: fields.name, setup: fields.setup };
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
    if (!line) throw new Error("The board did not answer. Is the CheerLights firmware installed? Try step 1, then connect again.");
    state.about = parseAbout(line);
    const chip = CHIP_NAMES[state.about.chip] || state.about.chip;
    const setUp = state.about.setup === "saved";
    setStatus($("connect-status"),
      `Connected to ${state.about.name}, an ${chip} running firmware ${state.about.fw}. ${setUp ? "It is set up already; you can change it below." : "It is not set up yet."}`,
      "ok");
    await loadChoices();
    $("step-setup").hidden = false;
    if (setUp) showDone();
  } catch (e) {
    if (e.name === "NotFoundError") setStatus($("connect-status"), "No port chosen.");
    else setStatus($("connect-status"), e.message, "error");
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
    $("preset-note").textContent = input.dataset.tested === "true"
      ? `${input.dataset.board}: the LEDs, brightness, and button are already known.`
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
    await save();
  } catch (e) {
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
  setStatus($("setup-status"), "Set up. The board is ready.", "ok");
  showDone();
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
    setStatus($("order-result"), "That is the same color twice. Let's try once more.", "error");
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
    else if ($("is-grid").checked && !(Number($("width").value) >= 2 && Number($("width").value) <= ledCount())) why = "The row width must be 2 or more, and no more than the number of LEDs.";
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
      if (!/^(\d+ LEDs? on GPIO|Color order|Serpentine rows|An RGB LED|Common anode|Brightness cap|No button|Button on GPIO)/.test(reply)) throw new Error(reply);
    }
    await save();
  } catch (e) {
    setStatus($("setup-status"), e.message, "error");
  } finally {
    busy(button, false);
  }
}

async function forget() {
  const button = $("forget");
  if (!state.forgetArmed) {
    state.forgetArmed = true;
    button.textContent = "Click again to forget: the LEDs stay off until you set it up again";
    setTimeout(() => { state.forgetArmed = false; button.textContent = "Forget its settings"; }, 6000);
    return;
  }
  state.forgetArmed = false;
  button.textContent = "Forget its settings";
  busy(button, true);
  try {
    await state.board.ask("reset", /^Settings forgotten/, 3000);
    await waitForRestart(state.board);
    $("step-app").hidden = true;
    setStatus($("setup-status"), "Settings forgotten. Choose what is connected to set it up again.");
    state.about.setup = "none";
  } catch (e) {
    setStatus($("setup-status"), e.message, "error");
  } finally {
    busy(button, false);
  }
}

function showDone() {
  $("device-name").textContent = state.about.name;
  $("step-app").hidden = false;
  showLiveColor();
}

// The color the board will show once the app connects: the live CheerLights color.
async function showLiveColor() {
  try {
    const r = await fetch(LIVE_URL);
    const last = await r.json();
    const name = (last.field1 || "").trim();
    const hex = (last.field2 || "").trim();
    if (!/^#[0-9a-f]{6}$/i.test(hex)) return;
    $("live-swatch").style.background = hex;
    $("live-text").textContent = `It will show the CheerLights color, which is ${name} right now.`;
    $("live").hidden = false;
  } catch {
    // The page works without it.
  }
}

function init() {
  if (!("serial" in navigator)) {
    $("browser-note").hidden = false;
    $("connect").disabled = true;
  }
  fetch("version.txt").then((r) => (r.ok ? r.text() : "")).then((v) => {
    if (v.trim()) $("version").textContent = v.trim();
  }).catch(() => {});

  // The installer holds the port while it runs; connect afterwards.
  $("install").addEventListener("closed", () => {
    setStatus($("connect-status"), "Firmware installed. Click Connect to set it up.", "ok");
  });

  $("connect").addEventListener("click", connect);
  $("choices").addEventListener("change", onChoice);
  $("save-preset").addEventListener("click", savePreset);
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
}

init();
