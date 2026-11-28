(() => {
  "use strict";

  const RESPONSE_PREFIX = "@TANDEM ";
  const BLE_SERVICE_UUID = "7a4e0001-6f72-4d61-9c2a-54414e44454d";
  const BLE_CMD_CHAR_UUID = "7a4e0002-6f72-4d61-9c2a-54414e44454d";
  const BLE_RESP_CHAR_UUID = "7a4e0003-6f72-4d61-9c2a-54414e44454d";
  const BLE_LIVE_CHAR_UUID = "7a4e0004-6f72-4d61-9c2a-54414e44454d";
  const BLE_WRITE_CHUNK_BYTES = 20;
  const HID_CONFIG_USAGE_PAGE = 0xFF54;
  const HID_CONFIG_USAGE_APP = 0x004D;
  const HID_FRAME_MAGIC = 0x54;
  const HID_FRAME_VERSION = 1;
  const HID_MSG_COMMAND = 1;
  const HID_MSG_RESPONSE = 2;
  const HID_MSG_LIVE = 3;
  const HID_FLAG_START = 0x10;
  const HID_FLAG_END = 0x20;
  const HID_RESERVED_MASK = 0xC0;
  const HID_HEADER_BYTES = 6;
  const HID_OVERHEAD_BYTES = 8;
  const AXIS_NAMES = ["X", "Y", "Z", "Rx", "Ry", "Rz", "Slider", "Dial"];
  const HAT_LABELS = ["Centered", "N", "NE", "E", "SE", "S", "SW", "W", "NW"];
  const STICK_MODES = ["USB_HID", "BLE_HID", "ESPNOW_TX", "BLE_CFG"];
  const NOISE_WINDOW_SIZE = 60;
  const CIRCULARITY_SECTORS = 36;

  const els = {
    unsupported: document.getElementById("banner-unsupported"),
    toast: document.getElementById("status-toast"),
    btnConnectHid: document.getElementById("btn-connect-hid"),
    btnConnect: document.getElementById("btn-connect"),
    btnConnectBle: document.getElementById("btn-connect-ble"),
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
    valFw: document.getElementById("val-fw"),
    valTransports: document.getElementById("val-transports"),
    deviceSettingsRow: document.getElementById("device-settings-row"),
    stickDevControls: document.getElementById("stick-dev-controls"),
    baseDevControls: document.getElementById("base-dev-controls"),
    selStickRole: document.getElementById("sel-stick-role"),
    selStickMode: document.getElementById("sel-stick-mode"),
    selBaseMode: document.getElementById("sel-base-mode"),
    telemetryPanel: document.getElementById("telemetry-panel"),
    tlmSampleHz: document.getElementById("tlm-sample-hz"),
    tlmTxHz: document.getElementById("tlm-tx-hz"),
    tlmPeer: document.getElementById("tlm-peer"),
    tlmRssi: document.getElementById("tlm-rssi"),
    tlmDrop: document.getElementById("tlm-drop"),
    tlmFailsafe: document.getElementById("tlm-failsafe"),
    tlmBattery: document.getElementById("tlm-battery"),
    tabBtnLive: document.getElementById("tab-btn-live"),
    tabBtnCalib: document.getElementById("tab-btn-calib"),
    panelLive: document.getElementById("panel-live"),
    panelCalib: document.getElementById("panel-calib"),
    calibReadonlyBanner: document.getElementById("calib-readonly-banner"),
    btnSourceGamepad: document.getElementById("btn-source-gamepad"),
    btnSourceDevice: document.getElementById("btn-source-device"),
    wrapShowAllBtns: document.getElementById("wrap-show-all-btns"),
    chkShowAllBtns: document.getElementById("chk-show-all-btns"),
    btnResetDiag: document.getElementById("btn-reset-diag"),
    liveCardsContainer: document.getElementById("live-cards-container"),
    axesGrid: document.getElementById("axes-grid"),
    hatBox: document.getElementById("hat-box"),
    valHat: document.getElementById("val-hat"),
    buttonsGrid: document.getElementById("buttons-grid"),
  };

  class SerialTransport {
    constructor(onMessage, onDisconnect) {
      this.label = "Serial";
      this.pillKey = "USB_SERIAL";
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
                let waiter = this.pending.get(msg.cmd);
                let waiterKey = msg.cmd;
                if (!waiter && msg.cmd === "unknown" && msg.ok === false && this.pending.size === 1) {
                  waiterKey = this.pending.keys().next().value;
                  waiter = this.pending.get(waiterKey);
                }
                if (waiter) {
                  clearTimeout(waiter.timer);
                  this.pending.delete(waiterKey);
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

  class BleTransport {
    constructor(onMessage, onDisconnect) {
      this.label = "BLE";
      this.pillKey = "BLE_CFG";
      this.onMessage = onMessage;
      this.onDisconnect = onDisconnect;
      this.device = null;
      this.server = null;
      this.cmdChar = null;
      this.respChar = null;
      this.liveChar = null;
      this.pending = new Map();
      this.closing = false;
      this.respBuf = "";
      this.liveBuf = "";
      this.decoder = new TextDecoder();
      this.writeQueue = Promise.resolve();
      this._onDisconnected = () => {
        if (!this.closing) {
          this._rejectPending("Disconnected");
          this.onDisconnect();
        }
      };
      this._onRespNotify = (ev) => {
        this.respBuf = this._consumeChunk(this.respBuf, ev.target.value, true);
      };
      this._onLiveNotify = (ev) => {
        this.liveBuf = this._consumeChunk(this.liveBuf, ev.target.value, false);
      };
    }

    _rejectPending(reason) {
      for (const [, waiter] of this.pending) {
        clearTimeout(waiter.timer);
        waiter.reject(new Error(reason));
      }
      this.pending.clear();
    }

    _consumeChunk(buf, dataView, resolvePending) {
      const text = this.decoder.decode(dataView, { stream: true });
      let combined = buf + text;
      let idx;
      while ((idx = combined.indexOf("\n")) >= 0) {
        const line = combined.slice(0, idx).replace(/\r$/, "");
        combined = combined.slice(idx + 1);
        if (line.startsWith(RESPONSE_PREFIX)) {
          try {
            const msg = JSON.parse(line.slice(RESPONSE_PREFIX.length));
            if (resolvePending) {
              let waiter = this.pending.get(msg.cmd);
              let waiterKey = msg.cmd;
              if (!waiter && msg.cmd === "unknown" && msg.ok === false && this.pending.size === 1) {
                waiterKey = this.pending.keys().next().value;
                waiter = this.pending.get(waiterKey);
              }
              if (waiter) {
                clearTimeout(waiter.timer);
                this.pending.delete(waiterKey);
                waiter.resolve(msg);
              }
            }
            this.onMessage(msg);
          } catch (_) {}
        }
      }
      return combined;
    }

    async connect() {
      this.closing = false;
      this.respBuf = "";
      this.liveBuf = "";
      this.device = await navigator.bluetooth.requestDevice({
        filters: [{ services: [BLE_SERVICE_UUID] }],
      });
      this.device.addEventListener("gattserverdisconnected", this._onDisconnected);
      this.server = await this.device.gatt.connect();
      const service = await this.server.getPrimaryService(BLE_SERVICE_UUID);
      this.cmdChar = await service.getCharacteristic(BLE_CMD_CHAR_UUID);
      this.respChar = await service.getCharacteristic(BLE_RESP_CHAR_UUID);
      this.liveChar = await service.getCharacteristic(BLE_LIVE_CHAR_UUID);

      this.respChar.addEventListener("characteristicvaluechanged", this._onRespNotify);
      this.liveChar.addEventListener("characteristicvaluechanged", this._onLiveNotify);
      await this.respChar.startNotifications();
      await this.liveChar.startNotifications();
    }

    async disconnect() {
      this.closing = true;
      this._rejectPending("Disconnected");
      try {
        if (this.respChar) {
          this.respChar.removeEventListener("characteristicvaluechanged", this._onRespNotify);
          await this.respChar.stopNotifications();
        }
      } catch (_) {}
      try {
        if (this.liveChar) {
          this.liveChar.removeEventListener("characteristicvaluechanged", this._onLiveNotify);
          await this.liveChar.stopNotifications();
        }
      } catch (_) {}
      try {
        if (this.device) {
          this.device.removeEventListener("gattserverdisconnected", this._onDisconnected);
          if (this.device.gatt && this.device.gatt.connected) {
            this.device.gatt.disconnect();
          }
        }
      } catch (_) {}
      this.device = null;
      this.server = null;
      this.cmdChar = null;
      this.respChar = null;
      this.liveChar = null;
    }

    async sendCommand(cmdLine, expectedCmd, timeoutMs = 4000) {
      if (!this.cmdChar) {
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

      this.writeQueue = this.writeQueue.then(async () => {
        for (let off = 0; off < encoded.length; off += BLE_WRITE_CHUNK_BYTES) {
          const slice = encoded.slice(off, off + BLE_WRITE_CHUNK_BYTES);
          if (this.cmdChar.properties.writeWithoutResponse) {
            await this.cmdChar.writeValueWithoutResponse(slice);
          } else {
            await this.cmdChar.writeValueWithResponse(slice);
          }
        }
      });

      try {
        await this.writeQueue;
      } catch (err) {
        const waiter = this.pending.get(expectedCmd);
        if (waiter) {
          clearTimeout(waiter.timer);
          this.pending.delete(expectedCmd);
        }
        throw err;
      }
      return promise;
    }
  }

  function crc16CcittFalse(bytes, len) {
    let crc = 0xFFFF;
    for (let i = 0; i < len; i++) {
      crc ^= bytes[i] << 8;
      for (let bit = 0; bit < 8; bit++) {
        crc = (crc & 0x8000) !== 0
          ? ((crc << 1) ^ 0x1021) & 0xFFFF
          : (crc << 1) & 0xFFFF;
      }
    }
    return crc;
  }

  function computeHidReportSize(reports) {
    if (!Array.isArray(reports) || reports.length === 0) {
      return null;
    }
    const report = reports[0];
    let totalBits = 0;
    for (const item of report.items || []) {
      totalBits += (item.reportSize || 0) * (item.reportCount || 0);
    }
    const bytes = Math.ceil(totalBits / 8);
    if (bytes <= 0) {
      return null;
    }
    return { reportId: report.reportId ?? 0, bytes };
  }

  function findHidConfigCollection(device) {
    for (const col of device.collections || []) {
      if (col.usagePage === HID_CONFIG_USAGE_PAGE && col.usage === HID_CONFIG_USAGE_APP) {
        const input = computeHidReportSize(col.inputReports);
        const output = computeHidReportSize(col.outputReports);
        if (input && output) {
          return {
            inReportId: input.reportId,
            inReportBytes: input.bytes,
            outReportId: output.reportId,
            outReportBytes: output.bytes,
          };
        }
      }
    }
    return null;
  }

  class WebHidTransport {
    constructor(onMessage, onDisconnect) {
      this.label = "WebHID";
      this.pillKey = "WEBHID";
      this.onMessage = onMessage;
      this.onDisconnect = onDisconnect;
      this.device = null;
      this.collection = null;
      this.pending = new Map();
      this.reassemblers = new Map();
      this.closing = false;
      this.nextSeq = 1;
      this.encoder = new TextEncoder();
      this.decoder = new TextDecoder();
      this.writeQueue = Promise.resolve();
      this._onInputReport = (ev) => {
        if (!this.collection || ev.reportId !== this.collection.inReportId) {
          return;
        }
        this._handleChunk(ev.data);
      };
      this._onHidDisconnect = (ev) => {
        if (this.device && ev.device === this.device && !this.closing) {
          this._rejectPending("Disconnected");
          this._detachListeners();
          this.device = null;
          this.collection = null;
          this.onDisconnect();
        }
      };
    }

    _rejectPending(reason) {
      for (const [, waiter] of this.pending) {
        clearTimeout(waiter.timer);
        waiter.reject(new Error(reason));
      }
      this.pending.clear();
    }

    _detachListeners() {
      if (this.device) {
        this.device.removeEventListener("inputreport", this._onInputReport);
      }
      if (navigator.hid) {
        navigator.hid.removeEventListener("disconnect", this._onHidDisconnect);
      }
    }

    _allocateSeq() {
      for (let attempt = 0; attempt < 255; attempt++) {
        const candidate = this.nextSeq;
        this.nextSeq = (this.nextSeq % 255) + 1;
        if (!this.pending.has(candidate)) {
          return candidate;
        }
      }
      throw new Error("Too many in-flight WebHID commands");
    }

    _handleChunk(dataView) {
      const bytes = new Uint8Array(
        dataView.buffer,
        dataView.byteOffset,
        dataView.byteLength
      );
      if (bytes.length < HID_OVERHEAD_BYTES) return;
      if (bytes[0] !== HID_FRAME_MAGIC || bytes[1] !== HID_FRAME_VERSION) return;

      const flagsType = bytes[2];
      if ((flagsType & HID_RESERVED_MASK) !== 0) return;

      const msgType = flagsType & 0x0F;
      if (msgType !== HID_MSG_RESPONSE && msgType !== HID_MSG_LIVE) return;

      const isStart = (flagsType & HID_FLAG_START) !== 0;
      const isEnd = (flagsType & HID_FLAG_END) !== 0;
      const seq = bytes[3];
      const chunkIdx = bytes[4];
      const payloadLen = bytes[5];

      if (payloadLen + HID_OVERHEAD_BYTES > bytes.length) return;

      const crcInputLen = HID_HEADER_BYTES + payloadLen;
      const wireCrc = bytes[crcInputLen] | (bytes[crcInputLen + 1] << 8);
      if (crc16CcittFalse(bytes, crcInputLen) !== wireCrc) return;

      let state = this.reassemblers.get(msgType);
      if (isStart) {
        if (chunkIdx !== 0) {
          this.reassemblers.delete(msgType);
          return;
        }
        state = { seq, nextIdx: 0, chunks: [], totalLen: 0 };
        this.reassemblers.set(msgType, state);
      } else if (!state || state.seq !== seq || state.nextIdx !== chunkIdx) {
        this.reassemblers.delete(msgType);
        return;
      }

      if (state.totalLen + payloadLen > 4096) {
        this.reassemblers.delete(msgType);
        return;
      }

      if (payloadLen > 0) {
        state.chunks.push(bytes.slice(HID_HEADER_BYTES, HID_HEADER_BYTES + payloadLen));
        state.totalLen += payloadLen;
      }

      if (!isEnd) {
        if (state.nextIdx >= 255) {
          this.reassemblers.delete(msgType);
          return;
        }
        state.nextIdx += 1;
        return;
      }

      this.reassemblers.delete(msgType);
      const merged = new Uint8Array(state.totalLen);
      let offset = 0;
      for (const slice of state.chunks) {
        merged.set(slice, offset);
        offset += slice.length;
      }

      const line = this.decoder.decode(merged).replace(/[\r\n]+$/, "");
      if (!line.startsWith(RESPONSE_PREFIX)) return;

      try {
        const msg = JSON.parse(line.slice(RESPONSE_PREFIX.length));
        if (msgType === HID_MSG_RESPONSE) {
          const waiter = this.pending.get(seq);
          if (waiter) {
            clearTimeout(waiter.timer);
            this.pending.delete(seq);
            waiter.resolve(msg);
          }
        }
        this.onMessage(msg);
      } catch (_) {}
    }

    async connect() {
      this.closing = false;
      this.reassemblers.clear();
      const devices = await navigator.hid.requestDevice({
        filters: [{ usagePage: HID_CONFIG_USAGE_PAGE, usage: HID_CONFIG_USAGE_APP }],
      });
      if (!devices || devices.length === 0) {
        throw new Error("No WebHID device selected");
      }
      const dev = devices[0];
      const info = findHidConfigCollection(dev);
      if (!info) {
        throw new Error("Selected device does not expose a Tandem Controls WebHID collection");
      }
      if (
        info.inReportBytes < HID_OVERHEAD_BYTES + 1 ||
        info.outReportBytes < HID_OVERHEAD_BYTES + 1
      ) {
        throw new Error("Unsupported WebHID report size");
      }
      if (!dev.opened) {
        await dev.open();
      }
      this.device = dev;
      this.collection = info;
      this.device.addEventListener("inputreport", this._onInputReport);
      navigator.hid.addEventListener("disconnect", this._onHidDisconnect);
    }

    async disconnect() {
      this.closing = true;
      this._rejectPending("Disconnected");
      this.reassemblers.clear();
      this._detachListeners();
      try {
        if (this.device && this.device.opened) {
          await this.device.close();
        }
      } catch (_) {}
      this.device = null;
      this.collection = null;
    }

    async sendCommand(cmdLine, expectedCmd, timeoutMs = 3000) {
      if (!this.device || !this.device.opened || !this.collection) {
        throw new Error("Not connected");
      }

      const payload = this.encoder.encode(`$TANDEM ${cmdLine}\n`);
      const outBytes = this.collection.outReportBytes;
      const maxPayload = outBytes - HID_OVERHEAD_BYTES;
      const totalChunks = payload.length === 0 ? 1 : Math.ceil(payload.length / maxPayload);
      if (totalChunks > 256) {
        throw new Error("Command exceeds WebHID chunk limit");
      }

      const seq = this._allocateSeq();
      const promise = new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
          this.pending.delete(seq);
          reject(new Error(`Timeout waiting for '${expectedCmd}'`));
        }, timeoutMs);
        this.pending.set(seq, { resolve, reject, timer });
      });

      this.writeQueue = this.writeQueue.then(async () => {
        if (!this.device || !this.device.opened) {
          throw new Error("Not connected");
        }
        for (let idx = 0; idx < totalChunks; idx++) {
          const offset = idx * maxPayload;
          const sliceLen = Math.min(maxPayload, payload.length - offset);
          const chunk = new Uint8Array(outBytes);
          let flags = HID_MSG_COMMAND;
          if (idx === 0) flags |= HID_FLAG_START;
          if (idx + 1 === totalChunks) flags |= HID_FLAG_END;

          chunk[0] = HID_FRAME_MAGIC;
          chunk[1] = HID_FRAME_VERSION;
          chunk[2] = flags;
          chunk[3] = seq;
          chunk[4] = idx;
          chunk[5] = sliceLen;
          if (sliceLen > 0) {
            chunk.set(payload.subarray(offset, offset + sliceLen), HID_HEADER_BYTES);
          }
          const crc = crc16CcittFalse(chunk, HID_HEADER_BYTES + sliceLen);
          chunk[HID_HEADER_BYTES + sliceLen] = crc & 0xFF;
          chunk[HID_HEADER_BYTES + sliceLen + 1] = (crc >> 8) & 0xFF;

          await this.device.sendReport(this.collection.outReportId, chunk);
        }
      });

      try {
        await this.writeQueue;
      } catch (err) {
        const waiter = this.pending.get(seq);
        if (waiter) {
          clearTimeout(waiter.timer);
          this.pending.delete(seq);
        }
        throw err;
      }

      return promise;
    }
  }

  const state = {
    connected: false,
    caps: null,
    axes: [],
    live: { raw: new Array(8).fill(0), out: new Array(8).fill(0), btns: 0, hat: 0, synth: false },
    tlm: null,
    streamFrames: 0,
    streamRateWindowMs: performance.now(),
    streamHz: 0,
    sweeping: false,
    sweepBounds: [],
    activeTab: "live",
    liveSource: "gamepad",
    diagByCard: new Map(),
    renderedLiveKey: "",
    gamepadRafId: null,
    expectedDisconnectNotice: "",
  };

  const hasHid = "hid" in navigator;
  const hasSerial = "serial" in navigator;
  const hasBle = "bluetooth" in navigator;
  const hasGamepadApi = "getGamepads" in navigator;
  const webHidTransport = new WebHidTransport(handleMessage, handleUnexpectedDisconnect);
  const serialTransport = new SerialTransport(handleMessage, handleUnexpectedDisconnect);
  const bleTransport = new BleTransport(handleMessage, handleUnexpectedDisconnect);
  let transport = serialTransport;

  function showToast(text, kind = "ok") {
    els.toast.textContent = text;
    els.toast.className = `banner banner-${kind}`;
  }

  function clearToast() {
    els.toast.className = "banner hidden";
  }

  function roleDisplay(role) {
    if (role === "L") return { text: "Left Stick", cls: "role-left" };
    if (role === "R") return { text: "Right Stick", cls: "role-right" };
    if (role === "BASE") return { text: "Base Station", cls: "role-base" };
    return { text: role || "—", cls: "" };
  }

  function resetDiagnostics(cardKey) {
    if (cardKey) {
      state.diagByCard.delete(cardKey);
    } else {
      state.diagByCard.clear();
    }
  }

  function getCardDiag(cardKey, axisCount) {
    let diag = state.diagByCard.get(cardKey);
    if (!diag) {
      diag = {
        axes: Array.from({ length: axisCount }, () => []),
        sectors: new Float64Array(CIRCULARITY_SECTORS),
      };
      state.diagByCard.set(cardKey, diag);
    }
    while (diag.axes.length < axisCount) {
      diag.axes.push([]);
    }
    return diag;
  }

  function recordDiagSample(cardKey, norms, rawSamples, sectorsActive) {
    const diag = getCardDiag(cardKey, Math.max(2, norms.length));
    for (let i = 0; i < norms.length; i++) {
      const win = diag.axes[i];
      win.push({
        norm: norms[i],
        raw: rawSamples ? rawSamples[i] : norms[i],
      });
      if (win.length > NOISE_WINDOW_SIZE) {
        win.shift();
      }
    }
    if (sectorsActive && norms.length >= 2) {
      const x = norms[0];
      const y = norms[1];
      const r = Math.hypot(x, y);
      if (r >= 0.5) {
        let angle = Math.atan2(y, x);
        if (angle < 0) angle += Math.PI * 2;
        const sector = Math.min(
          CIRCULARITY_SECTORS - 1,
          Math.floor((angle / (Math.PI * 2)) * CIRCULARITY_SECTORS)
        );
        if (r > diag.sectors[sector]) {
          diag.sectors[sector] = r;
        }
      }
    }
    return diag;
  }

  function summarizeAxisWindow(samples) {
    if (!samples || samples.length === 0) {
      return { meanRaw: 0, meanNorm: 0, spreadRaw: 0, spreadNorm: 0, stdNorm: 0 };
    }
    let minR = samples[0].raw;
    let maxR = samples[0].raw;
    let minN = samples[0].norm;
    let maxN = samples[0].norm;
    let sumR = 0;
    let sumN = 0;
    for (const s of samples) {
      if (s.raw < minR) minR = s.raw;
      if (s.raw > maxR) maxR = s.raw;
      if (s.norm < minN) minN = s.norm;
      if (s.norm > maxN) maxN = s.norm;
      sumR += s.raw;
      sumN += s.norm;
    }
    const meanR = sumR / samples.length;
    const meanN = sumN / samples.length;
    let sq = 0;
    for (const s of samples) {
      const d = s.norm - meanN;
      sq += d * d;
    }
    return {
      meanRaw: meanR,
      meanNorm: meanN,
      spreadRaw: maxR - minR,
      spreadNorm: maxN - minN,
      stdNorm: Math.sqrt(sq / samples.length),
    };
  }

  function computeCircularityStats(sectors) {
    let covered = 0;
    let errSum = 0;
    let maxErr = 0;
    for (let s = 0; s < CIRCULARITY_SECTORS; s++) {
      const r = sectors[s];
      if (r > 0) {
        covered++;
        const e = Math.abs(r - 1.0);
        errSum += e;
        if (e > maxErr) maxErr = e;
      }
    }
    return {
      covered,
      avgErrPct: covered > 0 ? (errSum / covered) * 100 : null,
      maxErrPct: covered > 0 ? maxErr * 100 : null,
    };
  }

  function switchTab(tab) {
    state.activeTab = tab;
    const isLive = tab === "live";
    els.tabBtnLive.classList.toggle("active", isLive);
    els.tabBtnLive.setAttribute("aria-selected", String(isLive));
    els.tabBtnCalib.classList.toggle("active", !isLive);
    els.tabBtnCalib.setAttribute("aria-selected", String(!isLive));
    els.panelLive.classList.toggle("hidden", !isLive);
    els.panelCalib.classList.toggle("hidden", isLive);
    syncGamepadPolling();
  }

  function setLiveSource(source) {
    if (source === "device" && !state.connected) {
      return;
    }
    state.liveSource = source;
    els.btnSourceGamepad.classList.toggle("active", source === "gamepad");
    els.btnSourceDevice.classList.toggle("active", source === "device");
    els.wrapShowAllBtns.classList.toggle("hidden", source !== "device");
    state.renderedLiveKey = "";
    renderLiveCards();
    syncGamepadPolling();
  }

  function updateIdentityBadges(caps) {
    if (!caps) {
      els.valBoard.textContent = "—";
      els.valRole.textContent = "—";
      els.valRole.className = "role-badge";
      els.valMode.textContent = "—";
      els.valInput.textContent = "—";
      els.valFw.textContent = "—";
      els.valTransports.textContent = "—";
      return;
    }
    const r = roleDisplay(caps.role);
    els.valRole.textContent = r.text;
    els.valRole.className = `role-badge ${r.cls}`;
    els.valBoard.textContent = caps.board || "UNKNOWN";
    els.valMode.textContent = caps.mode || "UNKNOWN";
    els.valInput.textContent = caps.synth ? "SYNTHETIC" : "LIVE";
    const fw = caps.fw || "?";
    const git = caps.git ? ` (${caps.git})` : "";
    els.valFw.textContent = `${fw}${git}`;

    els.valTransports.innerHTML = "";
    if (Array.isArray(caps.transports) && caps.transports.length > 0) {
      for (const tx of caps.transports) {
        const pill = document.createElement("span");
        const isMode =
          tx === caps.mode ||
          (caps.role === "BASE" && (tx === "USB_HID" || tx === "ESPNOW_RX"));
        const isLink = tx === transport?.pillKey;
        const classes = ["tx-pill"];
        if (isMode) classes.push("active");
        if (isLink && !isMode) classes.push("active-link");
        pill.className = classes.join(" ");
        if (isMode && isLink) {
          pill.title = "Active mode & configurator transport";
        } else if (isMode) {
          pill.title = "Active device mode";
        } else if (isLink) {
          pill.title = "Active configurator transport";
        }
        pill.textContent = tx;
        els.valTransports.appendChild(pill);
      }
    } else {
      els.valTransports.textContent = caps.mode || "—";
    }

    if (caps.role === "BASE") {
      els.selBaseMode.value = caps.mode === "ESPNOW_RX_COMBINED" ? "COMBINED" : "DUAL";
    } else {
      if (caps.role === "L" || caps.role === "R") {
        els.selStickRole.value = caps.role;
      }
      const supportedModes = Array.isArray(caps.transports)
        ? caps.transports.filter((tx) => STICK_MODES.includes(tx))
        : [];
      if (caps.mode && STICK_MODES.includes(caps.mode) && !supportedModes.includes(caps.mode)) {
        supportedModes.push(caps.mode);
      }
      els.selStickMode.innerHTML = "";
      for (const mode of supportedModes) {
        const opt = document.createElement("option");
        opt.value = mode;
        opt.textContent = mode;
        els.selStickMode.appendChild(opt);
      }
      if (caps.mode) {
        els.selStickMode.value = caps.mode;
      }
    }
  }

  function updateTelemetryDom() {
    if (!state.connected) {
      els.telemetryPanel.classList.add("hidden");
      return;
    }
    els.telemetryPanel.classList.remove("hidden");
    const t = state.tlm || {};
    els.tlmSampleHz.textContent =
      typeof t.hz === "number" ? `${t.hz} Hz` : "N/A";
    const txBase = typeof t.tx_hz === "number" ? `${t.tx_hz} Hz` : "N/A";
    els.tlmTxHz.textContent =
      state.streamHz > 0 ? `${txBase} (UI ${state.streamHz} Hz)` : txBase;
    els.tlmPeer.textContent = t.peer || state.caps?.peer || "N/A";
    els.tlmRssi.textContent =
      typeof t.rssi === "number" ? `${t.rssi} dBm` : "N/A";
    els.tlmDrop.textContent =
      typeof t.drop === "number" ? String(t.drop) : "N/A";

    if (typeof t.fs === "boolean") {
      els.tlmFailsafe.textContent = t.fs ? "ACTIVE" : "OK";
      els.tlmFailsafe.className = `tlm-pill ${t.fs ? "fault" : "ok"}`;
    } else {
      els.tlmFailsafe.textContent = "N/A";
      els.tlmFailsafe.className = "tlm-pill";
    }

    if (typeof t.bat_mv === "number" && typeof t.bat_pct === "number") {
      els.tlmBattery.textContent = `${(t.bat_mv / 1000).toFixed(2)} V (${t.bat_pct}%)`;
    } else if (typeof t.bat_mv === "number") {
      els.tlmBattery.textContent = `${(t.bat_mv / 1000).toFixed(2)} V`;
    } else if (typeof t.bat_pct === "number") {
      els.tlmBattery.textContent = `${t.bat_pct}%`;
    } else {
      els.tlmBattery.textContent = "N/A";
    }
  }

  function setConnectedUi(connected) {
    state.connected = connected;
    const cfgAllowed = connected && state.caps?.cfg !== false;
    els.btnConnectHid.disabled = connected || !hasHid;
    els.btnConnect.disabled = connected || !hasSerial;
    els.btnConnectBle.disabled = connected || !hasBle;
    els.btnDisconnect.disabled = !connected;
    els.btnReset.disabled = !connected;
    els.btnSave.disabled = !cfgAllowed;
    els.btnCenterAll.disabled = !cfgAllowed;
    els.btnSweepAll.disabled = !cfgAllowed;
    els.btnApplyAll.disabled = !cfgAllowed;
    els.btnSourceDevice.disabled = !connected;
    els.valStatus.textContent = connected ? `Connected (${transport.label})` : "Disconnected";

    const isBase = connected && state.caps?.role === "BASE";
    els.deviceSettingsRow.classList.toggle("hidden", !connected);
    els.stickDevControls.classList.toggle("hidden", !connected || isBase);
    els.baseDevControls.classList.toggle("hidden", !connected || !isBase);

    if (!connected) {
      state.caps = null;
      state.axes = [];
      state.tlm = null;
      state.streamHz = 0;
      updateIdentityBadges(null);
      updateTelemetryDom();
      els.calibReadonlyBanner.classList.add("hidden");
      if (state.liveSource === "device") {
        setLiveSource("gamepad");
      }
      renderAxes();
      renderDigital();
    } else {
      els.calibReadonlyBanner.classList.toggle("hidden", cfgAllowed);
      updateTelemetryDom();
    }
  }

  function handleMessage(msg) {
    if (msg.cmd === "caps" && msg.ok && state.connected) {
      const prevMode = state.caps?.mode;
      const prevRole = state.caps?.role;
      state.caps = msg;
      updateIdentityBadges(msg);
      if (prevMode !== msg.mode || prevRole !== msg.role) {
        state.renderedLiveKey = "";
        if (state.liveSource === "device" && state.activeTab === "live") {
          renderLiveCards();
        }
        if (prevMode && prevMode !== msg.mode) {
          showToast(`Device mode changed to ${msg.mode}.`, "ok");
        } else if (prevRole && prevRole !== msg.role) {
          showToast(`Device role changed to ${roleDisplay(msg.role).text}.`, "ok");
        }
      }
      return;
    }

    if (msg.cmd === "status") {
      if (msg.tlm) {
        state.tlm = msg.tlm;
        updateTelemetryDom();
      }
      return;
    }

    if (msg.cmd === "live" && Array.isArray(msg.raw) && Array.isArray(msg.out)) {
      state.live = msg;
      if (msg.tlm) {
        state.tlm = msg.tlm;
      }
      const now = performance.now();
      state.streamFrames++;
      if (now - state.streamRateWindowMs >= 1000) {
        state.streamHz = Math.round(
          (state.streamFrames * 1000) / (now - state.streamRateWindowMs)
        );
        state.streamFrames = 0;
        state.streamRateWindowMs = now;
      }
      els.valInput.textContent = msg.synth ? "SYNTHETIC" : "LIVE";
      updateTelemetryDom();

      if (state.sweeping) {
        for (let i = 0; i < 8; i++) {
          const r = msg.raw[i];
          const b = state.sweepBounds[i];
          if (r < b.min) b.min = r;
          if (r > b.max) b.max = r;
        }
      }
      updateCalibrationLiveDom();
      if (state.liveSource === "device" && state.activeTab === "live") {
        updateDeviceLiveCard();
      }
    }
  }

  function handleUnexpectedDisconnect() {
    const notice = state.expectedDisconnectNotice;
    state.expectedDisconnectNotice = "";
    setConnectedUi(false);
    state.sweeping = false;
    els.btnSweepAll.textContent = "Start Min/Max Sweep";
    els.btnSweepAll.className = "btn btn-secondary";
    showToast(notice || "Device connection closed.", "warn");
  }

  function buildHatSvg(cardKey) {
    const coords = [
      [46, 46], // 0 Centered
      [46, 12], // 1 N
      [70, 22], // 2 NE
      [80, 46], // 3 E
      [70, 70], // 4 SE
      [46, 80], // 5 S
      [22, 70], // 6 SW
      [12, 46], // 7 W
      [22, 22], // 8 NW
    ];
    const dots = coords
      .map(
        ([cx, cy], idx) =>
          `<circle id="hat-dot-${cardKey}-${idx}" cx="${cx}" cy="${cy}" r="${idx === 0 ? 5 : 4}" fill="${idx === 0 ? "#10b981" : "#2c3547"}" />`
      )
      .join("");
    return `
      <svg width="92" height="92" viewBox="0 0 92 92">
        <circle cx="46" cy="46" r="38" fill="none" stroke="#2c3547" stroke-width="1.5" />
        <line x1="46" y1="8" x2="46" y2="84" stroke="#1f2634" stroke-width="1" />
        <line x1="8" y1="46" x2="84" y2="46" stroke="#1f2634" stroke-width="1" />
        ${dots}
      </svg>
    `;
  }

  function createLiveCardElement({ cardKey, title, badgeText, axisIndices, buttonCount, hasHat }) {
    const card = document.createElement("div");
    card.className = "live-card";
    card.dataset.cardKey = cardKey;

    const axisRowsHtml = axisIndices
      .map(
        (idx) => `
      <div class="live-axis-row">
        <span>${AXIS_NAMES[idx] ?? `A${idx}`}</span>
        <div class="bar-track"><div class="bar-fill shaped" id="live-bar-${cardKey}-${idx}"></div></div>
        <span class="live-axis-num" id="live-num-${cardKey}-${idx}">0</span>
      </div>`
      )
      .join("");

    const btnPillsHtml = Array.from({ length: buttonCount }, (_, idx) => {
      return `<div class="btn-pill" id="live-btn-${cardKey}-${idx}">B${idx + 1}</div>`;
    }).join("");

    const hatHtml = hasHat
      ? `
      <div class="hat-compass">
        ${buildHatSvg(cardKey)}
        <strong id="live-hat-label-${cardKey}" class="hat-compass-label">Centered (0)</strong>
      </div>`
      : "";

    card.innerHTML = `
      <div class="live-card-head">
        <span class="live-card-title">${title}</span>
        <span class="badge">${badgeText}</span>
      </div>
      <div class="live-top-row">
        <div class="xy-box">
          <canvas class="xy-canvas" id="xy-canvas-${cardKey}" width="220" height="220"></canvas>
          <div class="xy-readout" id="xy-readout-${cardKey}">X: +0.000  Y: +0.000</div>
        </div>
        <div class="live-axes-list">${axisRowsHtml}</div>
      </div>
      <div class="live-digital-row">
        ${hatHtml}
        <div class="live-buttons-wrap">${btnPillsHtml}</div>
      </div>
      <div class="diag-panel">
        <div class="diag-metric">
          <span class="diag-label">X Rest-Offset &amp; Jitter</span>
          <strong id="diag-x-${cardKey}">—</strong>
        </div>
        <div class="diag-metric">
          <span class="diag-label">Y Rest-Offset &amp; Jitter</span>
          <strong id="diag-y-${cardKey}">—</strong>
        </div>
        <div class="diag-metric">
          <span class="diag-label">X/Y Circularity Error</span>
          <strong id="diag-circ-${cardKey}">Move full rim (0/36)</strong>
        </div>
      </div>
    `;
    return card;
  }

  function drawXyCanvas(cardKey, xNorm, yNorm, sectors) {
    const canvas = document.getElementById(`xy-canvas-${cardKey}`);
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    const w = canvas.width;
    const h = canvas.height;
    const cx = w / 2;
    const cy = h / 2;
    const radius = 88;

    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = "#0f1218";
    ctx.fillRect(0, 0, w, h);

    // Square limit box (-1..+1)
    ctx.strokeStyle = "#1f2634";
    ctx.lineWidth = 1;
    ctx.strokeRect(cx - radius, cy - radius, radius * 2, radius * 2);

    // Crosshairs
    ctx.strokeStyle = "#2c3547";
    ctx.beginPath();
    ctx.moveTo(cx - radius - 8, cy);
    ctx.lineTo(cx + radius + 8, cy);
    ctx.moveTo(cx, cy - radius - 8);
    ctx.lineTo(cx, cy + radius + 8);
    ctx.stroke();

    // Reference circles (50% and 100%)
    ctx.beginPath();
    ctx.arc(cx, cy, radius * 0.5, 0, Math.PI * 2);
    ctx.strokeStyle = "#1f2634";
    ctx.stroke();

    ctx.beginPath();
    ctx.arc(cx, cy, radius, 0, Math.PI * 2);
    ctx.strokeStyle = "#334155";
    ctx.stroke();

    // Circularity outer rim trace
    const pts = [];
    for (let s = 0; s < CIRCULARITY_SECTORS; s++) {
      if (sectors[s] > 0) {
        const ang = ((s + 0.5) / CIRCULARITY_SECTORS) * Math.PI * 2;
        const rPx = Math.min(1.18, sectors[s]) * radius;
        pts.push([cx + Math.cos(ang) * rPx, cy + Math.sin(ang) * rPx]);
      }
    }
    if (pts.length >= 3) {
      ctx.beginPath();
      ctx.moveTo(pts[0][0], pts[0][1]);
      for (let i = 1; i < pts.length; i++) {
        ctx.lineTo(pts[i][0], pts[i][1]);
      }
      ctx.closePath();
      ctx.fillStyle = "rgba(59, 130, 246, 0.18)";
      ctx.fill();
      ctx.strokeStyle = "#3b82f6";
      ctx.lineWidth = 1.5;
      ctx.stroke();
    }

    // Current stick vector + point
    const px = cx + Math.max(-1.15, Math.min(1.15, xNorm)) * radius;
    const py = cy + Math.max(-1.15, Math.min(1.15, yNorm)) * radius;
    ctx.beginPath();
    ctx.moveTo(cx, cy);
    ctx.lineTo(px, py);
    ctx.strokeStyle = "rgba(16, 185, 129, 0.55)";
    ctx.lineWidth = 1.5;
    ctx.stroke();

    ctx.beginPath();
    ctx.arc(px, py, 5.5, 0, Math.PI * 2);
    ctx.fillStyle = "#10b981";
    ctx.fill();
  }

  function updateHatCompass(cardKey, hatVal) {
    const clamped = hatVal >= 0 && hatVal <= 8 ? hatVal : 0;
    for (let idx = 0; idx <= 8; idx++) {
      const dot = document.getElementById(`hat-dot-${cardKey}-${idx}`);
      if (dot) {
        dot.setAttribute("fill", idx === clamped ? "#10b981" : "#2c3547");
      }
    }
    const label = document.getElementById(`live-hat-label-${cardKey}`);
    if (label) {
      label.textContent = `${HAT_LABELS[clamped] ?? "?"} (${clamped})`;
    }
  }

  function updateCardDiagnosticsDom(cardKey, diag, rawCfgAxes) {
    const circ = computeCircularityStats(diag.sectors);
    const circEl = document.getElementById(`diag-circ-${cardKey}`);
    if (circEl) {
      if (circ.covered === 0) {
        circEl.textContent = "Move full rim (0/36)";
      } else {
        circEl.textContent = `avg ${circ.avgErrPct.toFixed(1)}% | max ${circ.maxErrPct.toFixed(1)}% (${circ.covered}/36)`;
      }
    }

    for (const [axisIdx, elId] of [
      [0, `diag-x-${cardKey}`],
      [1, `diag-y-${cardKey}`],
    ]) {
      const el = document.getElementById(elId);
      if (!el) continue;
      const stats = summarizeAxisWindow(diag.axes[axisIdx]);
      if (rawCfgAxes && rawCfgAxes[axisIdx]) {
        const ax = rawCfgAxes[axisIdx];
        const offsetCnt = stats.meanRaw - ax.ctr;
        const halfSpan = Math.max(1, (ax.max - ax.min) / 2);
        const offsetPct = (offsetCnt / halfSpan) * 100;
        el.textContent = `${offsetCnt >= 0 ? "+" : ""}${offsetCnt.toFixed(1)} cnt (${offsetPct >= 0 ? "+" : ""}${offsetPct.toFixed(1)}%) | span ${stats.spreadRaw.toFixed(0)} cnt`;
      } else {
        el.textContent = `off ${stats.meanNorm >= 0 ? "+" : ""}${(stats.meanNorm * 100).toFixed(2)}% | jit ${(stats.spreadNorm * 100).toFixed(2)}% (σ ${(stats.stdNorm * 100).toFixed(2)}%)`;
      }
    }
  }

  function getDeviceButtonIndices() {
    if (!state.caps || els.chkShowAllBtns.checked) {
      return Array.from({ length: 32 }, (_, i) => i);
    }
    const mask = state.caps.btns_mask >>> 0;
    const list = [];
    for (let b = 0; b < 32; b++) {
      if (((mask >>> b) & 1) === 1) {
        list.push(b);
      }
    }
    return list.length > 0 ? list : Array.from({ length: 16 }, (_, i) => i);
  }

  function getDeviceAxisIndices() {
    if (!state.caps) {
      return [0, 1, 2, 3];
    }
    const mask = state.caps.axes_mask || 0;
    const list = [];
    for (let i = 0; i < 8; i++) {
      if ((mask & (1 << i)) !== 0) {
        list.push(i);
      }
    }
    if (!list.includes(0)) list.unshift(0);
    if (!list.includes(1)) list.splice(1, 0, 1);
    return list;
  }

  function renderLiveCards() {
    if (state.liveSource === "device") {
      if (!state.connected || !state.caps) {
        els.liveCardsContainer.innerHTML = `<p class="muted">Connect a Tandem Controls device to view its live stream.</p>`;
        state.renderedLiveKey = "device-empty";
        return;
      }
      const axes = getDeviceAxisIndices();
      const btns = getDeviceButtonIndices();
      const key = `device-${axes.join(",")}-${btns.length}-${state.caps.hat ? 1 : 0}`;
      if (state.renderedLiveKey === key) return;
      state.renderedLiveKey = key;

      const r = roleDisplay(state.caps.role);
      const card = createLiveCardElement({
        cardKey: "device",
        title: `${r.text} — ${state.caps.board || "Tandem Controls"}`,
        badgeText: state.caps.mode || "LIVE",
        axisIndices: axes,
        buttonCount: btns.length,
        hasHat: Boolean(state.caps.hat),
      });
      // Remap button pill IDs to actual hardware button indices when filtered
      const pills = card.querySelectorAll(".live-buttons-wrap .btn-pill");
      pills.forEach((pill, pos) => {
        const hwBit = btns[pos];
        pill.id = `live-btn-device-${hwBit}`;
        pill.textContent = `B${hwBit + 1}`;
      });

      els.liveCardsContainer.innerHTML = "";
      els.liveCardsContainer.appendChild(card);
      updateDeviceLiveCard();
      return;
    }

    // Browser Gamepad API mode
    if (!hasGamepadApi) {
      els.liveCardsContainer.innerHTML = `<p class="muted">Gamepad API is not available in this browser.</p>`;
      state.renderedLiveKey = "gp-unsupported";
      return;
    }

    const pads = Array.from(navigator.getGamepads()).filter(Boolean);
    if (pads.length === 0) {
      if (state.renderedLiveKey !== "gp-empty") {
        state.renderedLiveKey = "gp-empty";
        els.liveCardsContainer.innerHTML = `<p class="muted">No active browser Gamepad detected yet. Plug in or pair a USB/BLE controller and press any button or move a stick to wake Chrome/Edge's Gamepad API, or connect above via <strong>Connect WebHID</strong> / <strong>Connect Serial</strong> / <strong>Connect BLE</strong>.</p>`;
      }
      return;
    }

    const key = `gp-${pads.map((p) => `${p.index}:${p.axes.length}:${p.buttons.length}`).join("|")}`;
    if (state.renderedLiveKey !== key) {
      state.renderedLiveKey = key;
      const frag = document.createDocumentFragment();
      for (const gp of pads) {
        const cardKey = `gp-${gp.index}`;
        const axisIndices = Array.from({ length: Math.min(8, gp.axes.length) }, (_, i) => i);
        const card = createLiveCardElement({
          cardKey,
          title: gp.id,
          badgeText: `Gamepad #${gp.index} (${gp.mapping || "raw"})`,
          axisIndices,
          buttonCount: Math.min(32, gp.buttons.length),
          hasHat: gp.axes.length >= 10 || gp.buttons.length >= 16,
        });
        frag.appendChild(card);
      }
      els.liveCardsContainer.innerHTML = "";
      els.liveCardsContainer.appendChild(frag);
    }
  }

  function updateDeviceLiveCard() {
    const out = state.live.out || [];
    const raw = state.live.raw || [];
    const xNorm = (out[0] ?? 0) / 32767;
    const yNorm = (out[1] ?? 0) / 32767;
    const norms = out.map((v) => (v ?? 0) / 32767);
    const diag = recordDiagSample("device", norms, raw, true);

    drawXyCanvas("device", xNorm, yNorm, diag.sectors);
    const xyEl = document.getElementById("xy-readout-device");
    if (xyEl) {
      xyEl.textContent = `X: ${out[0] ?? 0}  Y: ${out[1] ?? 0}`;
    }

    for (let i = 0; i < 8; i++) {
      const bar = document.getElementById(`live-bar-device-${i}`);
      const num = document.getElementById(`live-num-device-${i}`);
      if (!bar || !num) continue;
      const val = out[i] ?? 0;
      const pct = Math.min(50, (Math.abs(val) / 32767) * 50);
      bar.style.width = `${pct}%`;
      bar.style.left = val >= 0 ? "50%" : `${50 - pct}%`;
      num.textContent = `${val} (${raw[i] ?? 0})`;
    }

    if (state.caps?.hat) {
      updateHatCompass("device", state.live.hat ?? 0);
    }

    const maskBtns = state.live.btns >>> 0;
    for (let b = 0; b < 32; b++) {
      const pill = document.getElementById(`live-btn-device-${b}`);
      if (pill) {
        pill.classList.toggle("pressed", ((maskBtns >>> b) & 1) === 1);
      }
    }

    const cfgAxes = state.caps?.cfg !== false && state.axes.length === 8 ? state.axes : null;
    updateCardDiagnosticsDom("device", diag, cfgAxes);
  }

  function decodeGamepadHat(gp) {
    // Check standard HID hat axis mapped by Chromium on axis 9 (values ~ -1.0 .. +1.0 for 1..8, >1.1 centered)
    if (gp.axes.length >= 10) {
      const h = gp.axes[9];
      if (typeof h === "number" && h >= -1.05 && h <= 1.05) {
        const idx = Math.round(((h + 1.0) / 2.0) * 7.0) + 1;
        if (idx >= 1 && idx <= 8) return idx;
      }
    }
    // Fallback to W3C standard mapping D-pad buttons 12..15 (Up, Down, Left, Right)
    if (gp.mapping === "standard" && gp.buttons.length >= 16) {
      const n = gp.buttons[12]?.pressed;
      const s = gp.buttons[13]?.pressed;
      const w = gp.buttons[14]?.pressed;
      const e = gp.buttons[15]?.pressed;
      if (n && e) return 2;
      if (s && e) return 4;
      if (s && w) return 6;
      if (n && w) return 8;
      if (n) return 1;
      if (e) return 3;
      if (s) return 5;
      if (w) return 7;
    }
    return 0;
  }

  function pollGamepadsFrame() {
    if (state.activeTab !== "live" || state.liveSource !== "gamepad" || !hasGamepadApi) {
      state.gamepadRafId = null;
      return;
    }
    renderLiveCards();
    const pads = Array.from(navigator.getGamepads()).filter(Boolean);
    for (const gp of pads) {
      const cardKey = `gp-${gp.index}`;
      const norms = gp.axes.slice(0, 8).map((v) => (Number.isFinite(v) ? v : 0));
      const diag = recordDiagSample(cardKey, norms, null, true);
      const xNorm = norms[0] ?? 0;
      const yNorm = norms[1] ?? 0;

      drawXyCanvas(cardKey, xNorm, yNorm, diag.sectors);
      const xyEl = document.getElementById(`xy-readout-${cardKey}`);
      if (xyEl) {
        xyEl.textContent = `X: ${xNorm >= 0 ? "+" : ""}${xNorm.toFixed(3)}  Y: ${yNorm >= 0 ? "+" : ""}${yNorm.toFixed(3)}`;
      }

      for (let i = 0; i < norms.length; i++) {
        const bar = document.getElementById(`live-bar-${cardKey}-${i}`);
        const num = document.getElementById(`live-num-${cardKey}-${i}`);
        if (!bar || !num) continue;
        const val = norms[i];
        const pct = Math.min(50, Math.abs(val) * 50);
        bar.style.width = `${pct}%`;
        bar.style.left = val >= 0 ? "50%" : `${50 - pct}%`;
        num.textContent = `${val >= 0 ? "+" : ""}${val.toFixed(3)}`;
      }

      updateHatCompass(cardKey, decodeGamepadHat(gp));

      const btnCount = Math.min(32, gp.buttons.length);
      for (let b = 0; b < btnCount; b++) {
        const pill = document.getElementById(`live-btn-${cardKey}-${b}`);
        if (pill) {
          pill.classList.toggle("pressed", Boolean(gp.buttons[b]?.pressed));
        }
      }
      updateCardDiagnosticsDom(cardKey, diag, null);
    }
    state.gamepadRafId = requestAnimationFrame(pollGamepadsFrame);
  }

  function syncGamepadPolling() {
    const shouldPoll =
      state.activeTab === "live" && state.liveSource === "gamepad" && hasGamepadApi;
    if (shouldPoll && state.gamepadRafId === null) {
      state.gamepadRafId = requestAnimationFrame(pollGamepadsFrame);
    } else if (!shouldPoll && state.gamepadRafId !== null) {
      cancelAnimationFrame(state.gamepadRafId);
      state.gamepadRafId = null;
    }
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
      els.axesGrid.innerHTML = `<p class="muted">Connect a Tandem Controls stick over USB (<strong>WebHID</strong> or <strong>WebSerial</strong>) or BLE (<code>BLE_CFG</code> mode — no OS Bluetooth pairing needed).</p>`;
      return;
    }
    if (state.caps.cfg === false) {
      els.axesGrid.innerHTML = `<p class="muted">Calibration controls are disabled for Base Station connections. Use the Live Tester tab to inspect incoming dual-stick telemetry.</p>`;
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
    updateCalibrationLiveDom();
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

  function updateCalibrationLiveDom() {
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
    state.caps = caps;
    updateIdentityBadges(caps);

    if (caps.cfg !== false) {
      const cfg = await transport.sendCommand("get", "get");
      state.axes = cfg.axes || [];
    } else {
      state.axes = [];
    }

    try {
      const st = await transport.sendCommand("status", "status");
      if (st?.tlm) {
        state.tlm = st.tlm;
      }
    } catch (_) {}

    setConnectedUi(true);
    renderAxes();
    renderDigital();
  }

  async function connectWith(selectedTransport) {
    clearToast();
    state.expectedDisconnectNotice = "";
    transport = selectedTransport;
    try {
      await transport.connect();
      await refreshConfig();
      resetDiagnostics("device");
      setLiveSource("device");
      if (state.caps?.cfg === false) {
        switchTab("live");
      }
      await transport.sendCommand("live 1", "live");
      showToast("Connected to Tandem Controls device.", "ok");
    } catch (err) {
      await transport.disconnect();
      setConnectedUi(false);
      showToast(`Connection failed: ${err.message}`, "err");
    }
  }

  els.btnConnectHid.addEventListener("click", () => connectWith(webHidTransport));
  els.btnConnect.addEventListener("click", () => connectWith(serialTransport));
  els.btnConnectBle.addEventListener("click", () => connectWith(bleTransport));

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
    const wasBase = state.caps?.role === "BASE";
    const rebootMsg =
      "Factory defaults restored; Base Station is rebooting to re-enumerate USB. Reconnect once ready.";
    if (wasBase) {
      state.expectedDisconnectNotice = rebootMsg;
    }
    try {
      const res = await transport.sendCommand("reset", "reset");
      if (res.ok) {
        if (res.reboot) {
          state.expectedDisconnectNotice = rebootMsg;
          showToast(rebootMsg, "warn");
        } else {
          state.expectedDisconnectNotice = "";
          await refreshConfig();
          showToast("Reset device settings and stick config to defaults.", "ok");
        }
      } else {
        state.expectedDisconnectNotice = "";
        showToast(`Reset failed: ${res.err}`, "err");
      }
    } catch (err) {
      if (wasBase && err.message === "Disconnected") {
        showToast(rebootMsg, "warn");
      } else {
        state.expectedDisconnectNotice = "";
        showToast(`Reset failed: ${err.message}`, "err");
      }
    }
  });

  els.selStickRole.addEventListener("change", async () => {
    const nextRole = els.selStickRole.value;
    const prevRole = state.caps?.role;
    try {
      const res = await transport.sendCommand(`role ${nextRole}`, "role");
      if (!res.ok) {
        if (prevRole) els.selStickRole.value = prevRole;
        showToast(`Role change rejected: ${res.err}`, "err");
        return;
      }
      if (state.caps) {
        state.caps.role = res.role || nextRole;
        updateIdentityBadges(state.caps);
        if (state.liveSource === "device" && state.activeTab === "live") {
          state.renderedLiveKey = "";
          renderLiveCards();
        }
      }
      showToast("Stick role saved to flash.", "ok");
    } catch (err) {
      if (prevRole) els.selStickRole.value = prevRole;
      showToast(`Role change failed: ${err.message}`, "err");
    }
  });

  els.selStickMode.addEventListener("change", async () => {
    const nextMode = els.selStickMode.value;
    const prevMode = state.caps?.mode;
    const expectBleClose = transport === bleTransport && nextMode !== "BLE_CFG";
    if (expectBleClose) {
      state.expectedDisconnectNotice = `Stick saved ${nextMode} and switched modes (BLE Config session closed).`;
    }
    try {
      const res = await transport.sendCommand(`mode ${nextMode}`, "mode");
      if (!res.ok) {
        state.expectedDisconnectNotice = "";
        if (prevMode) els.selStickMode.value = prevMode;
        showToast(`Mode change rejected: ${res.err}`, "err");
        return;
      }
      if (expectBleClose) {
        const msg = `Stick saved ${nextMode} and is switching modes (BLE Config session closing).`;
        state.expectedDisconnectNotice = msg;
        showToast(msg, "warn");
        return;
      }
      state.expectedDisconnectNotice = "";
      if (state.caps) {
        state.caps.mode = res.mode || nextMode;
        updateIdentityBadges(state.caps);
      }
      showToast(`Transport mode saved (${res.mode || nextMode}).`, "ok");
    } catch (err) {
      if (expectBleClose && err.message === "Disconnected") {
        showToast(`Stick saved ${nextMode} and switched modes (BLE Config session closed).`, "warn");
      } else {
        state.expectedDisconnectNotice = "";
        if (prevMode) els.selStickMode.value = prevMode;
        showToast(`Mode change failed: ${err.message}`, "err");
      }
    }
  });

  els.selBaseMode.addEventListener("change", async () => {
    const nextMode = els.selBaseMode.value;
    const prevMode = state.caps?.mode === "ESPNOW_RX_COMBINED" ? "COMBINED" : "DUAL";
    const expectReboot = nextMode !== prevMode;
    const rebootMsg = `Base Station saved ${nextMode} and is rebooting to re-enumerate USB. Reconnect once ready.`;
    if (expectReboot) {
      state.expectedDisconnectNotice = rebootMsg;
    }
    try {
      const res = await transport.sendCommand(`mode ${nextMode}`, "mode");
      if (!res.ok) {
        state.expectedDisconnectNotice = "";
        els.selBaseMode.value = prevMode;
        showToast(`Base mode change rejected: ${res.err}`, "err");
        return;
      }
      if (res.reboot) {
        state.expectedDisconnectNotice = rebootMsg;
        showToast(rebootMsg, "warn");
      } else {
        state.expectedDisconnectNotice = "";
        if (state.caps) {
          state.caps.mode = res.mode || state.caps.mode;
          updateIdentityBadges(state.caps);
        }
        showToast(`Base Station already in ${nextMode} mode.`, "ok");
      }
    } catch (err) {
      if (expectReboot && err.message === "Disconnected") {
        showToast(`Base Station switched to ${nextMode} and disconnected to re-enumerate USB. Reconnect once ready.`, "warn");
      } else {
        state.expectedDisconnectNotice = "";
        els.selBaseMode.value = prevMode;
        showToast(`Base mode change failed: ${err.message}`, "err");
      }
    }
  });

  els.btnSave.addEventListener("click", async () => {
    const res = await transport.sendCommand("save", "save");
    if (res.ok) {
      showToast("Saved configuration to flash.", "ok");
    } else if (res.err === "nvs_not_implemented") {
      showToast("Save to flash unavailable on this target; changes are active in RAM.", "warn");
    } else {
      showToast(`Save failed: ${res.err}`, "err");
    }
  });

  els.chkShowAllAxes.addEventListener("change", renderAxes);
  els.chkShowAllBtns.addEventListener("change", () => {
    state.renderedLiveKey = "";
    renderLiveCards();
  });
  els.tabBtnLive.addEventListener("click", () => switchTab("live"));
  els.tabBtnCalib.addEventListener("click", () => switchTab("calib"));
  els.btnSourceGamepad.addEventListener("click", () => setLiveSource("gamepad"));
  els.btnSourceDevice.addEventListener("click", () => setLiveSource("device"));
  els.btnResetDiag.addEventListener("click", () => {
    resetDiagnostics();
    showToast("Reset live diagnostics (noise window & circularity trace).", "ok");
  });

  window.addEventListener("gamepadconnected", () => {
    state.renderedLiveKey = "";
    renderLiveCards();
  });
  window.addEventListener("gamepaddisconnected", () => {
    state.renderedLiveKey = "";
    renderLiveCards();
  });

  if (!hasHid && !hasSerial && !hasBle) {
    els.unsupported.classList.remove("hidden");
  }
  els.btnConnectHid.disabled = !hasHid;
  els.btnConnect.disabled = !hasSerial;
  els.btnConnectBle.disabled = !hasBle;
  switchTab("live");
  renderLiveCards();
})();
