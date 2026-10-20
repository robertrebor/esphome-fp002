/*
 * FP002 face / palm-vein recognition - custom web UI for the ESPHome web_server.
 *
 * Replaces the default ESPHome entity list with a dashboard: module status,
 * last recognition, user enrollment with live 5-direction progress, user
 * management and an event log. It talks to the device only through the
 * standard ESPHome web_server API:
 *   - live state:  Server-Sent Events on /events ("state" events)
 *   - commands:    POST /<domain>/<entity name>/<action>
 *
 * Usage (ESPHome YAML):
 *   web_server:
 *     version: 3
 *     js_url: "https://cdn.jsdelivr.net/gh/robertrebor/esphome-fp002@<version>/web/fp002-ui.js"
 *   or, fully offline (file next to your YAML):
 *     version: 3
 *     js_url: ""
 *     js_include: "fp002-ui.js"
 *
 * Entity names below must match the `name:` values in your YAML (they do if
 * you kept the ones from example.yaml). Entities you didn't configure are
 * simply hidden.
 */
(() => {
  "use strict";

  const ENTITIES = {
    moduleReady: "binary_sensor/Module Ready",
    faceRecognized: "binary_sensor/Face Recognized",
    userName: "text_sensor/Recognized User Name",
    userId: "sensor/Recognized User Id",
    userIds: "text_sensor/Registered User Ids",
    userCount: "sensor/Registered User Count",
    enrollStatus: "text_sensor/Enroll Status",
    enrollProgress: "sensor/Enroll Progress",
    lastEvent: "text_sensor/Last Event",
    newName: "text/New User Name",
    newAdmin: "switch/New User Is Admin",
    newPalm: "switch/New User Is Palm Vein",
    newQuick: "switch/New User Quick Enroll (single photo)",
    enrollButton: "button/Enroll New User",
    deleteId: "number/Delete User Id",
    deletePalm: "switch/Delete User Is Palm Vein",
    deleteButton: "button/Delete User By Id",
    deleteAllType: "select/Delete All Users - Which",
    deleteAllButton: "button/Delete ALL Users",
  };
  const DIRECTIONS = ["center", "right", "left", "down", "up"];
  const MAX_LOG = 200;

  const CSS = `
:root{--bg:#f4f5f7;--card:#fff;--text:#1d2129;--muted:#6b7280;--line:#e3e6ea;--accent:#2563eb;
--ok:#15803d;--ok-bg:#dcfce7;--bad:#b91c1c;--bad-bg:#fee2e2;--warn:#a16207;--warn-bg:#fef3c7;
--chip:#eef2ff;--radius:12px;font-family:system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
@media (prefers-color-scheme:dark){:root{--bg:#111318;--card:#1b1e25;--text:#e6e8ec;--muted:#9aa1ad;
--line:#2c313b;--accent:#60a5fa;--ok:#4ade80;--ok-bg:#14301f;--bad:#f87171;--bad-bg:#3a1717;
--warn:#facc15;--warn-bg:#3a3110;--chip:#232a3b}}
*{box-sizing:border-box}html,body{margin:0;background:var(--bg);color:var(--text)}
esp-app{display:none}
.fp-wrap{max-width:1100px;margin:0 auto;padding:16px}
.fp-head{display:flex;flex-wrap:wrap;gap:12px;align-items:center;justify-content:space-between;margin-bottom:16px}
.fp-head h1{font-size:20px;margin:0;font-weight:650}
.fp-head .sub{color:var(--muted);font-size:13px;margin-top:2px}
.fp-pills{display:flex;gap:8px;flex-wrap:wrap}
.pill{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;border-radius:999px;font-size:13px;
background:var(--line);color:var(--muted)}
.pill::before{content:"";width:8px;height:8px;border-radius:50%;background:currentColor}
.pill.ok{background:var(--ok-bg);color:var(--ok)}.pill.bad{background:var(--bad-bg);color:var(--bad)}
.fp-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(320px,1fr));gap:16px}
.card{background:var(--card);border:1px solid var(--line);border-radius:var(--radius);padding:16px}
.card h2{font-size:14px;text-transform:uppercase;letter-spacing:.04em;color:var(--muted);margin:0 0 12px;font-weight:600}
.big{font-size:28px;font-weight:650;line-height:1.2;word-break:break-word}
.muted{color:var(--muted);font-size:13px}
.recog{display:flex;align-items:center;gap:14px}
.avatar{width:56px;height:56px;border-radius:50%;display:grid;place-items:center;font-size:22px;font-weight:700;
background:var(--line);color:var(--muted);flex:none;transition:background .2s,color .2s}
.avatar.on{background:var(--ok-bg);color:var(--ok)}
label.field{display:block;font-size:13px;color:var(--muted);margin:10px 0 4px}
input[type=text],input[type=number],select{width:100%;padding:9px 10px;border:1px solid var(--line);border-radius:8px;
background:transparent;color:var(--text);font:inherit}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
.check{display:inline-flex;align-items:center;gap:6px;font-size:14px;margin:8px 14px 0 0;cursor:pointer}
button{font:inherit;border:0;border-radius:8px;padding:9px 14px;cursor:pointer;background:var(--accent);color:#fff}
button.ghost{background:transparent;color:var(--text);border:1px solid var(--line)}
button.danger{background:var(--bad)}button:disabled{opacity:.5;cursor:not-allowed}
.steps{display:grid;grid-template-columns:repeat(5,1fr);gap:6px;margin:14px 0 8px}
.step{text-align:center;padding:8px 2px;border-radius:8px;background:var(--line);color:var(--muted);font-size:12px}
.step.done{background:var(--ok-bg);color:var(--ok)}.step.now{outline:2px solid var(--accent);color:var(--text)}
.status{padding:8px 10px;border-radius:8px;background:var(--line);font-size:14px;margin-top:6px}
.status.ok{background:var(--ok-bg);color:var(--ok)}.status.bad{background:var(--bad-bg);color:var(--bad)}
.status.run{background:var(--warn-bg);color:var(--warn)}
.chips{display:flex;flex-wrap:wrap;gap:6px;margin:8px 0 4px}
.chip{background:var(--chip);border:1px solid var(--line);border-radius:999px;padding:3px 10px;font-size:13px;cursor:pointer;color:var(--text)}
.chip:hover{border-color:var(--accent)}
hr{border:0;border-top:1px solid var(--line);margin:14px 0}
.log{max-height:360px;overflow:auto;font-size:13px;margin:0;padding:0;list-style:none}
.log li{display:flex;gap:10px;padding:6px 0;border-bottom:1px solid var(--line)}
.log time{color:var(--muted);font-variant-numeric:tabular-nums;flex:none}
.log .t-ok{color:var(--ok)}.log .t-bad{color:var(--bad)}
.span2{grid-column:1/-1}
.hidden{display:none!important}
.toast{position:fixed;left:50%;bottom:16px;transform:translateX(-50%);background:var(--text);color:var(--card);
padding:8px 14px;border-radius:8px;font-size:14px;opacity:0;transition:opacity .2s;pointer-events:none}
.toast.show{opacity:1}
`;

  const HTML = `
<div class="fp-wrap">
  <div class="fp-head">
    <div><h1 id="fp-title">FP002 Face Recognition</h1><div class="sub" id="fp-sub">ESPHome</div></div>
    <div class="fp-pills"><span class="pill" id="fp-conn">Connecting</span><span class="pill" id="fp-mod">Module</span></div>
  </div>
  <div class="fp-grid">
    <section class="card" data-need="faceRecognized userName">
      <h2>Last recognition</h2>
      <div class="recog"><div class="avatar" id="fp-avatar">?</div>
        <div><div class="big" id="fp-name">-</div><div class="muted" id="fp-uid"></div></div></div>
      <div class="muted" id="fp-recog-state" style="margin-top:10px"></div>
    </section>

    <section class="card" data-need="enrollButton">
      <h2>Enroll new user</h2>
      <label class="field" for="fp-in-name">Name</label>
      <input type="text" id="fp-in-name" maxlength="31" placeholder="name" autocomplete="off">
      <div class="row">
        <label class="check"><input type="checkbox" id="fp-in-admin">Admin</label>
        <label class="check"><input type="checkbox" id="fp-in-palm">Palm vein</label>
        <label class="check"><input type="checkbox" id="fp-in-quick">Quick (single photo)</label>
      </div>
      <div class="row" style="margin-top:12px"><button id="fp-enroll">Start enroll</button></div>
      <div class="steps" id="fp-steps"></div>
      <div class="status" id="fp-enroll-status">Idle</div>
      <div class="muted" id="fp-enroll-hint" style="margin-top:8px"></div>
    </section>

    <section class="card" data-need="userIds">
      <h2>Registered users</h2>
      <div class="big" id="fp-count">-</div><div class="muted">face users, click an id to select it</div>
      <div class="chips" id="fp-chips"></div>
      <hr>
      <div data-need="deleteButton">
        <label class="field" for="fp-del-id">Delete user by id</label>
        <div class="row"><input type="number" id="fp-del-id" min="1" max="800" style="max-width:120px">
          <label class="check" style="margin-top:0"><input type="checkbox" id="fp-del-palm">Palm vein</label>
          <button class="ghost" id="fp-del">Delete</button></div>
      </div>
      <div data-need="deleteAllButton">
        <label class="field" for="fp-delall-type">Delete all users</label>
        <div class="row"><select id="fp-delall-type" style="max-width:220px"></select>
          <button class="danger" id="fp-delall">Delete all</button></div>
      </div>
    </section>

    <section class="card span2" data-need="lastEvent">
      <h2>Event log <span class="muted" style="text-transform:none;letter-spacing:0">(since this page was opened)</span></h2>
      <ul class="log" id="fp-log"></ul>
    </section>
  </div>
</div>
<div class="toast" id="fp-toast"></div>`;

  // ---------------------------------------------------------------- state
  const state = {};        // entity id -> last JSON message
  const present = new Set();
  let lastEventSeen = null;
  let firstEvent = true;
  let resync = false;      // set after an SSE reconnect: the device resends all states
  const $ = (id) => document.getElementById(id);

  function toast(msg) {
    const t = $("fp-toast");
    t.textContent = msg;
    t.classList.add("show");
    clearTimeout(toast._t);
    toast._t = setTimeout(() => t.classList.remove("show"), 2500);
  }

  async function post(entity, action, params) {
    const [domain, name] = [entity.slice(0, entity.indexOf("/")), entity.slice(entity.indexOf("/") + 1)];
    const qs = params ? "?" + new URLSearchParams(params).toString() : "";
    const r = await fetch(`/${domain}/${encodeURIComponent(name)}/${action}${qs}`, { method: "POST" });
    if (!r.ok) throw new Error(`${entity}: HTTP ${r.status}`);
  }
  const setSwitch = (e, on) => (present.has(e) ? post(e, on ? "turn_on" : "turn_off") : Promise.resolve());

  // ---------------------------------------------------------------- render
  function value(key) {
    const m = state[ENTITIES[key]];
    return m && m.value !== null ? m.value : undefined;
  }
  const num = (v) => (v === undefined || v === "" || Number.isNaN(Number(v)) ? undefined : Number(v));

  function addLog(text) {
    const li = document.createElement("li");
    const time = document.createElement("time");
    time.textContent = new Date().toLocaleTimeString();
    const span = document.createElement("span");
    span.textContent = text;
    if (/^(Recognized|Enrolled|Deleted|Module ready)/.test(text)) span.className = "t-ok";
    else if (/(Rejected|failed|restarted)/i.test(text)) span.className = "t-bad";
    li.append(time, span);
    const log = $("fp-log");
    log.prepend(li);
    while (log.children.length > MAX_LOG) log.lastChild.remove();
  }

  function renderVisibility() {
    document.querySelectorAll("[data-need]").forEach((el) => {
      const ok = el.dataset.need.split(" ").every((k) => present.has(ENTITIES[k]));
      el.classList.toggle("hidden", !ok);
    });
  }

  function renderSteps() {
    const progress = num(value("enrollProgress")) || 0;
    const status = String(value("enrollStatus") || "Idle");
    const running = /^Step|^Waiting/.test(status);
    const steps = $("fp-steps");
    steps.innerHTML = "";
    DIRECTIONS.forEach((d, i) => {
      const s = document.createElement("div");
      s.className = "step" + (i < progress ? " done" : "") + (running && i === progress ? " now" : "");
      s.textContent = d;
      steps.append(s);
    });
    const st = $("fp-enroll-status");
    st.textContent = status;
    st.className = "status" + (/^Done/.test(status) ? " ok" : /^Failed/.test(status) ? " bad" : running ? " run" : "");
    $("fp-enroll").disabled = running;
    $("fp-enroll-hint").textContent = running
      ? /palm/i.test(status)
        ? "Hold the open palm 15-20 cm in front of the module and move it slowly back and forth."
        : /single/i.test(status)
        ? "Look straight at the module from 45-65 cm."
        : `Turn your head slowly to "${DIRECTIONS[Math.min(progress, 4)]}" and hold it until the LED blinks green.`
      : "";
  }

  function render(changedId) {
    const ready = value("moduleReady");
    const mod = $("fp-mod");
    mod.className = "pill " + (ready === true ? "ok" : "bad") + (present.has(ENTITIES.moduleReady) ? "" : " hidden");
    mod.textContent = ready === true ? "Module ready" : "Module not ready";

    const recognized = value("faceRecognized") === true;
    const name = value("userName");
    $("fp-name").textContent = name || "-";
    const uid = num(value("userId"));
    $("fp-uid").textContent = uid !== undefined ? `ID ${uid}` : "";
    const av = $("fp-avatar");
    av.textContent = name ? name.trim().charAt(0).toUpperCase() : "?";
    av.classList.toggle("on", recognized);
    $("fp-recog-state").textContent = recognized ? "Recognized now" : "Nobody recognized right now";

    const count = num(value("userCount"));
    $("fp-count").textContent = count !== undefined ? count : "-";
    if (changedId === ENTITIES.userIds || changedId === undefined) {
      const chips = $("fp-chips");
      chips.innerHTML = "";
      String(value("userIds") || "")
        .split(",")
        .filter(Boolean)
        .forEach((id) => {
          const c = document.createElement("button");
          c.className = "chip";
          c.textContent = id;
          c.title = "Select for delete";
          c.onclick = () => {
            $("fp-del-id").value = id;
            $("fp-del-palm").checked = Number(id) > 500;
          };
          chips.append(c);
        });
    }

    const sel = state[ENTITIES.deleteAllType];
    if (sel && Array.isArray(sel.option) && $("fp-delall-type").options.length === 0) {
      sel.option.forEach((o) => $("fp-delall-type").append(new Option(o, o)));
    }

    renderSteps();
  }

  // ---------------------------------------------------------------- events
  function onState(msg) {
    if (!msg || !msg.id) return;
    const isNew = !present.has(msg.id);
    state[msg.id] = Object.assign(state[msg.id] || {}, msg);
    present.add(msg.id);
    if (isNew) renderVisibility();
    if (msg.id === ENTITIES.lastEvent) {
      const text = String(msg.value || "");
      if (text) {
        // The first value on connect is the event that happened before the page
        // opened; after a reconnect the device resends the current value, which
        // is only new if it differs from what we already logged.
        if (firstEvent) addLog(`${text}  (before page load)`);
        else if (!(resync && text === lastEventSeen)) addLog(text);
      }
      firstEvent = false;
      resync = false;
      lastEventSeen = text;
    }
    render(msg.id);
  }

  function connect() {
    const conn = $("fp-conn");
    const es = new EventSource("/events");
    es.addEventListener("ping", (e) => {
      conn.className = "pill ok";
      conn.textContent = "Connected";
      try {
        const p = JSON.parse(e.data || "{}");
        if (p.title) {
          $("fp-title").textContent = p.title;
          document.title = p.title;
        }
        if (p.comment) $("fp-sub").textContent = p.comment;
      } catch (_) { /* ping without JSON */ }
    });
    es.addEventListener("state", (e) => {
      conn.className = "pill ok";
      conn.textContent = "Connected";
      try {
        onState(JSON.parse(e.data));
      } catch (err) {
        console.warn("fp002-ui: bad state event", err);
      }
    });
    es.onerror = () => {
      conn.className = "pill bad";
      conn.textContent = "Disconnected";
      resync = true;
    };
  }

  // ---------------------------------------------------------------- actions
  async function enroll() {
    const name = $("fp-in-name").value.trim();
    if (!name) {
      toast("Enter a name first");
      return;
    }
    $("fp-enroll").disabled = true;
    try {
      await post(ENTITIES.newName, "set", { value: name });
      await setSwitch(ENTITIES.newAdmin, $("fp-in-admin").checked);
      await setSwitch(ENTITIES.newPalm, $("fp-in-palm").checked);
      await setSwitch(ENTITIES.newQuick, $("fp-in-quick").checked);
      await post(ENTITIES.enrollButton, "press");
      toast(`Enroll of "${name}" started`);
    } catch (err) {
      toast(String(err.message || err));
      $("fp-enroll").disabled = false;
    }
  }

  async function deleteOne() {
    const id = Number($("fp-del-id").value);
    if (!id) {
      toast("Enter a user id");
      return;
    }
    const palm = $("fp-del-palm").checked;
    if (!confirm(`Delete ${palm ? "palm-vein" : "face"} user ID ${id}?`)) return;
    try {
      await post(ENTITIES.deleteId, "set", { value: id });
      await setSwitch(ENTITIES.deletePalm, palm);
      await post(ENTITIES.deleteButton, "press");
      toast(`Deleting user ID ${id}`);
    } catch (err) {
      toast(String(err.message || err));
    }
  }

  async function deleteAll() {
    const which = $("fp-delall-type").value;
    if (!which) return;
    if (!confirm(`Delete ${which.toLowerCase()}? This cannot be undone.`)) return;
    try {
      await post(ENTITIES.deleteAllType, "set", { option: which });
      await post(ENTITIES.deleteAllButton, "press");
      toast("Deleting users");
    } catch (err) {
      toast(String(err.message || err));
    }
  }

  // ---------------------------------------------------------------- boot
  function boot() {
    const style = document.createElement("style");
    style.textContent = CSS;
    document.head.append(style);
    if (!document.querySelector('meta[name="viewport"]')) {
      const vp = document.createElement("meta");
      vp.name = "viewport";
      vp.content = "width=device-width,initial-scale=1";
      document.head.append(vp);
    }
    const root = document.createElement("div");
    root.innerHTML = HTML;
    document.body.prepend(root);
    renderVisibility();
    render();
    $("fp-enroll").onclick = enroll;
    $("fp-del").onclick = deleteOne;
    $("fp-delall").onclick = deleteAll;
    connect();
  }

  if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", boot);
  else boot();
})();
