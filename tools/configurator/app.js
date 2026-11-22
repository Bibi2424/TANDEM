(() => {
  "use strict";

  const RESPONSE_PREFIX = "@TANDEM ";
  const AXIS_NAMES = ["X", "Y", "Z", "Rx", "Ry", "Rz", "Slider", "Dial"];
  const HAT_LABELS = ["Centered", "N", "NE", "E", "SE", "S", "SW", "W", "NW"];

  const els = {
    unsupported: document.getElementById("banner-unsupported"),
    toast: document.getElementById("status-toast"),
    btnConnect: document.getElementById("btn-connect"),
    btnDisconnect: document.getElementById("btn-disconnect"),
    btnReset: document.getElementById("btn-reset"),
    btnSave: document.getElementById("btn-save"),
    btnCenterAll: document.getElementById("btn-center-all"),
    btnSweepAll: document.getElementById("btn-sweep-all"),
    btnApplyAll: document.getElementById("btn-apply-all"),
    chkShowAllAxes: document.getElementById("chk-show-all-axes"),
    valStatus: document.getElementById("val-status"),
    valBoard: document.getElementById("val-board"),
    valRole: document.getElementById("val-role"),
    valMode: document.getElementById("val-mode"),
    valInput: document.getElementById("val-input"),
    axesGrid: document.getElementById("axes-grid"),
    hatBox: document.getElementById("hat-box"),
    valHat: document.getElementById("val-hat"),
    buttonsGrid: document.getElementById("buttons-grid"),
  };

  class SerialTransport {
    constructor(onMessage, onDisconnect) {
      this.onMessage = onMessage;
      this.onDisconnect = onDisconnect;
      this.port = null;
      this.reader = null;
      this.writer = null;
      this.readLoopPromise = null;
      this.pending = new Map();
      this.closing = false;
    }

    async connect() {
      this.closing = false;
      this.port = await navigator.serial.requestPort();
      await this.port.open({ baudRate: 115200 });
      this.writer = this.port.writable.getWriter();
      this.readLoopPromise = this._readLoop();
    }

    async disconnect() {
      this.closing = true;
      for (const [, waiter] of this.pending) {
        waiter.reject(new Error("Disconnected"));
      }
      this.pending.clear();
      try {
        if (this.reader) {
          await this.reader.cancel();
        }
      } catch (_) {}
      try {
        if (this.writer) {
          this.writer.releaseLock();
        }
      } catch (_) {}
      try {
        if (this.port) {
          await this.port.close();
        }
      } catch (_) {}
      this.port = null;
      this.reader = null;
      this.writer = null;
    }

    async sendCommand(cmdLine, expectedCmd, timeoutMs = 2000) {
      if (!this.writer) {
        throw new Error("Not connected");
      }
      const encoded = new TextEncoder().encode(`$TANDEM ${cmdLine}\n`);
      const promise = new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
          this.pending.delete(expectedCmd);
          reject(new Error(`Timeout waiting for '${expectedCmd}'`));
        }, timeoutMs);
        this.pending.set(expectedCmd, { resolve, reject, timer });
      });
      await this.writer.write(encoded);
      return promise;
    }

    async _readLoop() {
      const decoder = new TextDecoder();
      let buffer = "";
      try {
        this.reader = this.port.readable.getReader();
        while (!this.closing) {
          const { value, done } = await this.reader.read();
          if (done) {
            break;
          }
          buffer += decoder.decode(value, { stream: true });
          let idx;
          while ((idx = buffer.indexOf("\n")) >= 0) {
            const line = buffer.slice(0, idx).replace(/\r$/, "");
            buffer = buffer.slice(idx + 1);
            if (line.startsWith(RESPONSE_PREFIX)) {
              try {
                const msg = JSON.parse(line.slice(RESPONSE_PREFIX.length));
                const waiter = this.pending.get(msg.cmd);
                if (waiter) {
                  clearTimeout(waiter.timer);
                  this.pending.delete(msg.cmd);
                  waiter.resolve(msg);
                }
                this.onMessage(msg);
              } catch (_) {}
            }
          }
        }
      } catch (_) {
      } finally {
        if (this.reader) {
          try {
            this.reader.releaseLock();
          } catch (_) {}
          this.reader = null;
        }
        if (!this.closing) {
          this.onDisconnect();
        }
      }
    }
  }

  const state = {
    connected: false,
    caps: null,
    axes: [],
    live: { raw: new Array(8).fill(0), out: new Array(8).fill(0), btns: 0, hat: 0, synth: false },
    sweeping: false,
    sweepBounds: [],
  };

  const transport = new SerialTransport(handleMessage, handleUnexpectedDisconnect);

  function showToast(text, kind = "ok") {
    els.toast.textContent = text;
    els.toast.className = `banner banner-${kind}`;
  }

  function clearToast() {
    els.toast.className = "banner hidden";
  }

  function setConnectedUi(connected) {
    state.connected = connected;
    els.btnConnect.disabled = connected;
    els.btnDisconnect.disabled = !connected;
    els.btnReset.disabled = !connected;
    els.btnSave.disabled = !connected;
    els.btnCenterAll.disabled = !connected;
    els.btnSweepAll.disabled = !connected;
    els.btnApplyAll.disabled = !connected;
    els.valStatus.textContent = connected ? "Connected" : "Disconnected";
  }

  function handleMessage(msg) {
    if (msg.cmd === "live" && Array.isArray(msg.raw) && Array.isArray(msg.out)) {
      state.live = msg;
      els.valInput.textContent = msg.synth ? "SYNTHETIC" : "LIVE";
      if (state.sweeping) {
        for (let i = 0; i < 8; i++) {
          const r = msg.raw[i];
          const b = state.sweepBounds[i];
          if (r < b.min) b.min = r;
          if (r > b.max) b.max = r;
        }
      }
      updateLiveDom();
    }
  }

  function handleUnexpectedDisconnect() {
    setConnectedUi(false);
    state.sweeping = false;
    els.btnSweepAll.textContent = "Start Min/Max Sweep";
    els.btnSweepAll.className = "btn btn-secondary";
    showToast("Serial connection closed.", "warn");
  }

  function computeCurvePoints(deadzone, curveType, expoParam) {
    const pts = [];
    for (let s = 0; s <= 40; s++) {
      const x = s / 40;
      let y = 0;
      if (x > deadzone && deadzone < 1.0) {
        const rescaled = (x - deadzone) / (1.0 - deadzone);
        y = curveType === 1 ? (1 - expoParam) * rescaled + expoParam * Math.pow(rescaled, 3) : rescaled;
      }
      pts.push(`${(x * 80).toFixed(1)},${(44 - y * 40).toFixed(1)}`);
    }
    return pts.join(" ");
  }

  function renderAxes() {
    if (!state.caps || state.axes.length === 0) {
      els.axesGrid.innerHTML = `<p class="muted">Connect a T.A.N.D.E.M. stick over USB to inspect and calibrate axes.</p>`;
      return;
    }

    const showAll = els.chkShowAllAxes.checked;
    const frag = document.createDocumentFragment();

    state.axes.forEach((ax, i) => {
      const wired = (state.caps.axes_mask & (1 << i)) !== 0;
      if (!showAll && !wired) {
        return;
      }

      const card = document.createElement("div");
      card.className = "axis-card";
      card.dataset.axis = String(i);
      card.innerHTML = `
        <div class="axis-head">
          <span class="axis-title">Axis ${i} (${AXIS_NAMES[i]})${wired ? "" : " — Unwired"}</span>
          <label class="toggle-inline">
            <input type="checkbox" data-field="en" ${ax.en ? "checked" : ""} /> Enabled
          </label>
        </div>
        <div class="bar-row">
          <span>Raw</span>
          <div class="bar-track"><div class="bar-fill" id="raw-bar-${i}"></div></div>
          <span id="raw-val-${i}">0</span>
        </div>
        <div class="bar-row">
          <span>Shaped</span>
          <div class="bar-track"><div class="bar-fill shaped" id="out-bar-${i}"></div></div>
          <span id="out-val-${i}">0</span>
        </div>
        <div class="form-grid">
          <label class="field">Min
            <input type="number" data-field="min" value="${ax.min}" />
          </label>
          <label class="field">Center
            <input type="number" data-field="ctr" value="${ax.ctr}" />
          </label>
          <label class="field">Max
            <input type="number" data-field="max" value="${ax.max}" />
          </label>
          <label class="field">Deadzone
            <input type="number" step="0.01" min="0" max="0.95" data-field="dz" value="${ax.dz.toFixed(2)}" />
          </label>
          <label class="field">Curve
            <select data-field="crv">
              <option value="0" ${ax.crv === 0 ? "selected" : ""}>Linear</option>
              <option value="1" ${ax.crv === 1 ? "selected" : ""}>Expo</option>
            </select>
          </label>
          <label class="field">Expo
            <input type="number" step="0.05" min="0" max="1" data-field="p" value="${ax.p.toFixed(2)}" />
          </label>
        </div>
        <div class="axis-foot">
          <label class="toggle-inline">
            <input type="checkbox" data-field="inv" ${ax.inv ? "checked" : ""} /> Invert
          </label>
          <svg class="curve-preview" width="84" height="48" viewBox="0 0 84 48">
            <polyline id="curve-line-${i}" fill="none" stroke="#10b981" stroke-width="2"
              points="${computeCurvePoints(ax.dz, ax.crv, ax.p)}" />
          </svg>
          <button class="btn btn-primary" data-action="apply" data-axis="${i}">Apply</button>
        </div>
      `;

      card.addEventListener("input", () => {
        const dz = parseFloat(card.querySelector('[data-field="dz"]').value) || 0;
        const crv = parseInt(card.querySelector('[data-field="crv"]').value, 10) || 0;
        const p = parseFloat(card.querySelector('[data-field="p"]').value) || 0;
        card.querySelector(`#curve-line-${i}`).setAttribute("points", computeCurvePoints(dz, crv, p));
      });

      card.querySelector('[data-action="apply"]').addEventListener("click", () => applyAxis(i));
      frag.appendChild(card);
    });

    els.axesGrid.innerHTML = "";
    els.axesGrid.appendChild(frag);
    updateLiveDom();
  }

  function renderDigital() {
    if (!state.caps) {
      els.hatBox.classList.add("hidden");
      els.buttonsGrid.innerHTML = "";
      return;
    }
    els.hatBox.classList.toggle("hidden", !state.caps.hat);
    const frag = document.createDocumentFragment();
    for (let b = 0; b < 32; b++) {
      if ((state.caps.btns_mask & (1 << b)) === 0) {
        continue;
      }
      const pill = document.createElement("div");
      pill.className = "btn-pill";
      pill.id = `btn-pill-${b}`;
      pill.textContent = `B${b + 1}`;
      frag.appendChild(pill);
    }
    els.buttonsGrid.innerHTML = "";
    els.buttonsGrid.appendChild(frag);
  }

  function updateLiveDom() {
    const adcMax = state.caps ? Math.max(1, state.caps.adc_max) : 4095;
    for (let i = 0; i < 8; i++) {
      const rawEl = document.getElementById(`raw-val-${i}`);
      if (!rawEl) continue;
      const raw = state.live.raw[i] ?? 0;
      const out = state.live.out[i] ?? 0;
      rawEl.textContent = String(raw);
      document.getElementById(`raw-bar-${i}`).style.width = `${Math.min(100, Math.max(0, (raw / adcMax) * 100))}%`;

      document.getElementById(`out-val-${i}`).textContent = String(out);
      const outBar = document.getElementById(`out-bar-${i}`);
      const pct = Math.min(50, (Math.abs(out) / 32767) * 50);
      outBar.style.width = `${pct}%`;
      outBar.style.left = out >= 0 ? "50%" : `${50 - pct}%`;
    }

    if (state.caps?.hat) {
      const h = state.live.hat ?? 0;
      els.valHat.textContent = `${HAT_LABELS[h] ?? "?"} (${h})`;
    }
    for (let b = 0; b < 32; b++) {
      const pill = document.getElementById(`btn-pill-${b}`);
      if (pill) {
        pill.classList.toggle("pressed", ((state.live.btns >>> b) & 1) === 1);
      }
    }
  }

  function readAxisForm(i) {
    const card = els.axesGrid.querySelector(`.axis-card[data-axis="${i}"]`);
    if (!card) return state.axes[i];
    return {
      i,
      en: card.querySelector('[data-field="en"]').checked ? 1 : 0,
      min: parseInt(card.querySelector('[data-field="min"]').value, 10),
      ctr: parseInt(card.querySelector('[data-field="ctr"]').value, 10),
      max: parseInt(card.querySelector('[data-field="max"]').value, 10),
      dz: parseFloat(card.querySelector('[data-field="dz"]').value),
      inv: card.querySelector('[data-field="inv"]').checked ? 1 : 0,
      crv: parseInt(card.querySelector('[data-field="crv"]').value, 10),
      p: parseFloat(card.querySelector('[data-field="p"]').value),
    };
  }

  async function applyAxis(i) {
    const ax = readAxisForm(i);
    const cmd = `set ${ax.i} ${ax.en} ${ax.min} ${ax.ctr} ${ax.max} ${ax.dz.toFixed(4)} ${ax.inv} ${ax.crv} ${ax.p.toFixed(4)}`;
    const res = await transport.sendCommand(cmd, "set");
    if (!res.ok) {
      showToast(`Axis ${i} update rejected: ${res.err}`, "err");
      return false;
    }
    state.axes[i] = ax;
    showToast(`Axis ${i} updated in RAM.`, "ok");
    return true;
  }

  async function refreshConfig() {
    const caps = await transport.sendCommand("caps", "caps");
    const cfg = await transport.sendCommand("get", "get");
    state.caps = caps;
    state.axes = cfg.axes;
    els.valBoard.textContent = caps.board;
    els.valRole.textContent = caps.role === "L" ? "Left" : caps.role === "R" ? "Right" : caps.role;
    els.valMode.textContent = caps.mode;
    els.valInput.textContent = caps.synth ? "SYNTHETIC" : "LIVE";
    renderAxes();
    renderDigital();
  }

  els.btnConnect.addEventListener("click", async () => {
    clearToast();
    try {
      await transport.connect();
      setConnectedUi(true);
      await refreshConfig();
      await transport.sendCommand("live 1", "live");
      showToast("Connected to T.A.N.D.E.M. stick.", "ok");
    } catch (err) {
      await transport.disconnect();
      setConnectedUi(false);
      showToast(`Connection failed: ${err.message}`, "err");
    }
  });

  els.btnDisconnect.addEventListener("click", async () => {
    try {
      await transport.sendCommand("live 0", "live");
    } catch (_) {}
    await transport.disconnect();
    setConnectedUi(false);
    showToast("Disconnected.", "ok");
  });

  els.btnCenterAll.addEventListener("click", () => {
    for (let i = 0; i < 8; i++) {
      const card = els.axesGrid.querySelector(`.axis-card[data-axis="${i}"]`);
      if (card) {
        card.querySelector('[data-field="ctr"]').value = String(state.live.raw[i] ?? 2048);
      }
    }
    showToast("Captured live center values. Click Apply to send to stick.", "ok");
  });

  els.btnSweepAll.addEventListener("click", () => {
    if (!state.sweeping) {
      state.sweeping = true;
      state.sweepBounds = state.live.raw.map((v) => ({ min: v, max: v }));
      els.btnSweepAll.textContent = "Stop Sweep & Fill Min/Max";
      els.btnSweepAll.className = "btn btn-warn";
      showToast("Move all axes to full extremes, then click Stop Sweep.", "warn");
    } else {
      state.sweeping = false;
      els.btnSweepAll.textContent = "Start Min/Max Sweep";
      els.btnSweepAll.className = "btn btn-secondary";
      for (let i = 0; i < 8; i++) {
        const card = els.axesGrid.querySelector(`.axis-card[data-axis="${i}"]`);
        if (card) {
          card.querySelector('[data-field="min"]').value = String(state.sweepBounds[i].min);
          card.querySelector('[data-field="max"]').value = String(state.sweepBounds[i].max);
        }
      }
      showToast("Populated min/max from sweep. Click Apply to send to stick.", "ok");
    }
  });

  els.btnApplyAll.addEventListener("click", async () => {
    for (let i = 0; i < 8; i++) {
      if (els.axesGrid.querySelector(`.axis-card[data-axis="${i}"]`)) {
        const ok = await applyAxis(i);
        if (!ok) return;
      }
    }
    showToast("All visible axes applied to stick RAM.", "ok");
  });

  els.btnReset.addEventListener("click", async () => {
    const res = await transport.sendCommand("reset", "reset");
    if (res.ok) {
      await refreshConfig();
      showToast("Reset stick config to board defaults.", "ok");
    } else {
      showToast(`Reset failed: ${res.err}`, "err");
    }
  });

  els.btnSave.addEventListener("click", async () => {
    const res = await transport.sendCommand("save", "save");
    if (res.ok) {
      showToast("Saved configuration to flash.", "ok");
    } else if (res.err === "nvs_not_implemented") {
      showToast("Save to flash pending #33 (NVS persistence); changes are active in RAM.", "warn");
    } else {
      showToast(`Save failed: ${res.err}`, "err");
    }
  });

  els.chkShowAllAxes.addEventListener("change", renderAxes);

  if (!("serial" in navigator)) {
    els.unsupported.classList.remove("hidden");
    els.btnConnect.disabled = true;
  }
})();
