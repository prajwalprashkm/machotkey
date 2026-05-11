(function () {
  var PHASES = ["idle", "color", "ocr_fast", "ocr_accurate", "input", "opencv", "fs"];

  var CONFIG_KEYS = [
    ["TARGET_FPS", "Target FPS", 1, 240, 1],
    ["SUITE_PHASE_MS", "Suite phase (ms)", 500, 60000, 100],
    ["METRICS_WINDOW_FRAMES", "Metrics window (frames)", 10, 120, 1],
    ["COLOR_SEARCHES_PER_FRAME", "Color searches (full frame)", 1, 4096, 1],
    ["OPENCV_TEMPLATE_SIZE", "OpenCV template square (px)", 8, 256, 1],
    ["INPUT_GETPOSITION_BATCH", "get_position batch", 1, 4096, 1],
    ["FS_READS_PER_FRAME", "FS reads / frame", 1, 256, 1],
  ];

  var currentPhase = "idle";

  function hideOpencvModal() {
    var root = document.getElementById("opencvModal");
    if (!root) return;
    root.classList.remove("open");
    root.setAttribute("aria-hidden", "true");
  }

  function showOpencvModal() {
    var root = document.getElementById("opencvModal");
    if (!root) return;
    root.classList.add("open");
    root.setAttribute("aria-hidden", "false");
    var ok = document.getElementById("opencvModalOk");
    if (ok) ok.focus();
  }

  function syncOpencvModal(phase, opencvAcknowledged) {
    var pending = phase === "opencv" && opencvAcknowledged === false;
    if (pending) {
      showOpencvModal();
    } else {
      hideOpencvModal();
    }
  }

  function emit(name, payload) {
    if (window.macroUI && window.macroUI.emit) {
      window.macroUI.emit(name, payload != null ? payload : "");
    }
  }

  function setPhaseButtonHighlight() {
    document.querySelectorAll(".phase-grid button").forEach(function (btn) {
      btn.classList.toggle("active", btn.getAttribute("data-phase") === currentPhase);
    });
  }

  window.demoApplyLayout = function (data) {
    data = data || {};
    if (data.screen_w && data.screen_h) {
      var el = document.getElementById("liveMetrics");
      if (el && el.textContent.indexOf("FPS") < 0) {
        el.textContent = "Layout: " + data.screen_w + "×" + data.screen_h + " (from regions.init)";
      }
    }
  };

  window.demoApplyRuntime = function (data) {
    data = data || {};
    var ack = data.opencv_acknowledged;
    if (ack === undefined || ack === null) {
      ack = true;
    }
    if (data.phase) {
      currentPhase = data.phase;
      setPhaseButtonHighlight();
      syncOpencvModal(data.phase, ack);
    }
    var parts = [];
    if (data.phase) parts.push("phase=" + data.phase);
    if (data.fps !== undefined) parts.push("fps=" + data.fps.toFixed(2));
    if (data.raw_fps !== undefined) parts.push("raw_fps=" + data.raw_fps.toFixed(2));
    if (data.latency_ms !== undefined) parts.push("latency_ms=" + data.latency_ms.toFixed(3));
    if (data.dropped !== undefined) parts.push("dropped_total=" + data.dropped);
    if (data.paused !== undefined) parts.push("paused=" + data.paused);
    if (data.suite_running !== undefined) parts.push("suite=" + data.suite_running);
    if (data.phase === "opencv" && data.opencv_acknowledged === false) {
      parts.push("opencv=awaiting_OK");
    }
    if (data.workload_ops !== undefined && data.workload_ops > 0) {
      parts.push("ops=" + data.workload_ops);
      if (data.workload_us_per_op !== undefined) parts.push("us/op=" + data.workload_us_per_op.toFixed(1));
      if (data.workload_total_ms !== undefined) parts.push("batch_ms=" + data.workload_total_ms.toFixed(2));
    }
    var el = document.getElementById("liveMetrics");
    if (el) el.textContent = parts.join("   ");
  };

  window.demoApplyResults = function (obj) {
    var el = document.getElementById("resultsDump");
    if (!el) return;
    try {
      el.textContent = JSON.stringify(obj || {}, null, 2);
    } catch (e) {
      el.textContent = String(obj);
    }
  };

  window.demoApplyConfig = function (data) {
    data = data || {};
    CONFIG_KEYS.forEach(function (row) {
      var id = row[0];
      if (data[id] === undefined) return;
      var input = document.getElementById("cfg_" + id);
      if (input) input.value = String(data[id]);
    });
  };

  window.demoToast = function (msg, ok) {
    var t = document.getElementById("toast");
    t.textContent = msg;
    t.className = "toast show " + (ok ? "ok" : "err");
    clearTimeout(window.__demoToastT);
    window.__demoToastT = setTimeout(function () {
      t.className = "toast";
    }, 3000);
  };

  function buildPhaseButtons() {
    var root = document.getElementById("phaseButtons");
    PHASES.forEach(function (p) {
      var b = document.createElement("button");
      b.type = "button";
      b.setAttribute("data-phase", p);
      b.textContent = p;
      b.addEventListener("click", function () {
        emit("set_phase", "phase=" + p);
      });
      root.appendChild(b);
    });
    setPhaseButtonHighlight();
  }

  function buildConfigFields() {
    var root = document.getElementById("configFields");
    CONFIG_KEYS.forEach(function (row) {
      var id = row[0];
      var label = row[1];
      var min = row[2];
      var max = row[3];
      var step = row[4];
      var wrap = document.createElement("label");
      wrap.innerHTML =
        "<span>" +
        label +
        "</span><input id=\"cfg_" +
        id +
        "\" type=\"number\" min=\"" +
        min +
        "\" max=\"" +
        max +
        "\" step=\"" +
        step +
        "\" />";
      root.appendChild(wrap);
    });
  }

  function collectConfigSave() {
    return CONFIG_KEYS.map(function (row) {
      var id = row[0];
      var el = document.getElementById("cfg_" + id);
      if (!el) return null;
      var v = parseFloat(el.value);
      if (isNaN(v)) return null;
      return id + "=" + v;
    })
      .filter(Boolean)
      .join("\n");
  }

  document.getElementById("btnRunSuite").addEventListener("click", function () {
    emit("run_suite", "");
  });

  document.getElementById("btnStopSuite").addEventListener("click", function () {
    emit("stop_suite", "");
  });

  document.getElementById("btnCfgSave").addEventListener("click", function () {
    emit("config_save", collectConfigSave());
  });

  document.getElementById("btnCfgReset").addEventListener("click", function () {
    emit("config_reset", "");
  });

  (function wireOpencvModal() {
    var ok = document.getElementById("opencvModalOk");
    if (ok) {
      ok.addEventListener("click", function () {
        emit("opencv_ack", "");
      });
    }
  })();

  buildPhaseButtons();
  buildConfigFields();

  function emitReady() {
    if (window.macroUI && window.macroUI.emit) {
      emit("ui_ready", "");
      return;
    }
    setTimeout(emitReady, 16);
  }

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", emitReady);
  } else {
    emitReady();
  }
  window.addEventListener("load", emitReady);
})();
