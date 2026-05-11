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
      if (el && el.textContent.indexOf("callbacks_s") < 0) {
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
    if (data.fps !== undefined) parts.push("callbacks_s=" + data.fps.toFixed(2));
    if (data.raw_fps !== undefined) parts.push("callbacks_raw_s=" + data.raw_fps.toFixed(2));
    if (data.latency_ms !== undefined) parts.push("latency_ms=" + data.latency_ms.toFixed(3));
    if (data.dropped !== undefined) parts.push("dropped_total=" + data.dropped);
    if (data.paused !== undefined) parts.push("paused=" + data.paused);
    if (data.suite_running !== undefined) parts.push("suite=" + data.suite_running);
    if (data.phase === "opencv" && data.opencv_acknowledged === false) {
      parts.push("opencv=awaiting_OK");
    }
    if (data.workload_action_label && data.workload_action_hz > 0) {
      var avg = data.workload_action_avg_us_per_op;
      parts.push(
        data.workload_action_label +
          "=" +
          data.workload_action_hz.toFixed(0) +
          "/s" +
          (avg != null ? " avg_us/op=" + avg.toFixed(1) : "") +
          " (workload time)"
      );
    }
    if (data.workload_ops !== undefined && data.workload_ops > 0) {
      parts.push("ops=" + data.workload_ops);
      if (data.workload_us_per_op !== undefined) parts.push("us/op=" + data.workload_us_per_op.toFixed(1));
      if (data.workload_total_ms !== undefined) parts.push("batch_ms=" + data.workload_total_ms.toFixed(2));
    }
    if (data.runner_cpu_percent != null && data.runner_cpu_percent !== undefined) {
      parts.push("runner_cpu=" + data.runner_cpu_percent.toFixed(1) + "%");
    }
    if (data.runner_ram_mb != null && data.runner_ram_mb !== undefined) {
      parts.push("runner_ram=" + data.runner_ram_mb.toFixed(1) + "MB");
    }
    if (data.runner_vmem_mb != null && data.runner_vmem_mb !== undefined) {
      parts.push("runner_vmem=" + data.runner_vmem_mb.toFixed(1) + "MB");
    }
    var el = document.getElementById("liveMetrics");
    if (el) el.textContent = parts.join("   ");
  };

  function num(x) {
    var n = Number(x);
    return isFinite(n) ? n : null;
  }

  function fmtFixed(n, digits) {
    if (n == null) return "—";
    return n.toFixed(digits);
  }

  function fmtInt(n) {
    if (n == null) return "—";
    if (Math.abs(n) >= 1000) return Math.round(n).toLocaleString();
    return fmtFixed(n, n % 1 === 0 ? 0 : 1);
  }

  function el(tag, className, text) {
    var node = document.createElement(tag);
    if (className) node.className = className;
    if (text !== undefined && text !== null) node.textContent = text;
    return node;
  }

  function metricCell(label, value, primary) {
    var wrap = el("div", "bench-metric" + (primary ? " bench-metric--primary" : ""));
    wrap.appendChild(el("span", "bench-metric-label", label));
    wrap.appendChild(el("span", "bench-metric-value", value));
    return wrap;
  }

  function renderBenchVisual(obj) {
    var root = document.getElementById("resultsVisual");
    if (!root) return;
    obj = obj || {};
    var rows = [];
    PHASES.forEach(function (p) {
      if (obj[p] && typeof obj[p] === "object") rows.push(p);
    });
    root.innerHTML = "";
    if (rows.length === 0) {
      root.appendChild(
        el(
          "p",
          "bench-empty",
          "No completed metrics windows yet. Run the suite or switch phases — a card appears here after each full metrics window."
        )
      );
      return;
    }
    var maxCb = 0;
    rows.forEach(function (p) {
      var r = obj[p];
      var cb = num(r.callbacks_per_sec);
      if (cb != null && cb > maxCb) maxCb = cb;
    });
    var grid = el("div", "bench-grid");
    rows.forEach(function (p) {
      var r = obj[p];
      var cb = num(r.callbacks_per_sec);
      var raw = num(r.callback_raw_fps);
      var lat = num(r.latency_ms);
      var usOp = num(r.action_avg_us_per_op);
      var opsPerSec = num(r.action_ops_per_sec);
      if (opsPerSec == null) opsPerSec = num(r.action_ops_per_sec_workload_only);
      var actionName = r.action_label != null ? String(r.action_label) : "—";
      var barPct = maxCb > 0 && cb != null ? Math.min(100, Math.round((100 * cb) / maxCb)) : 0;

      var card = el("article", "bench-card");
      var head = el("div", "bench-card-head");
      head.appendChild(el("h3", "bench-phase", p));
      var sub = el("p", "bench-action-label", actionName);
      head.appendChild(sub);
      card.appendChild(head);

      var barTrack = el("div", "bench-bar-track");
      var barFill = el("div", "bench-bar-fill");
      barFill.style.width = barPct + "%";
      barTrack.appendChild(barFill);
      card.appendChild(barTrack);
      card.appendChild(el("p", "bench-bar-caption", "Capture callbacks/s vs best phase in this view (" + barPct + "% of peak)"));

      var metrics = el("div", "bench-metrics");
      metrics.appendChild(metricCell("Callbacks / s", fmtFixed(cb, 1), true));
      metrics.appendChild(metricCell("Raw callbacks / s", fmtFixed(raw, 1), true));
      metrics.appendChild(metricCell("Latency (ms)", fmtFixed(lat, 3), false));
      metrics.appendChild(metricCell("Action ops / s (workload time)", fmtInt(opsPerSec), true));
      metrics.appendChild(metricCell("Avg µs / op", fmtFixed(usOp, 1), false));
      card.appendChild(metrics);

      var last = el("div", "bench-last-batch");
      var lo = num(r.workload_ops_last_batch);
      var lus = num(r.workload_us_per_op_last_batch);
      var lms = num(r.workload_total_ms_last_batch);
      last.appendChild(
        el(
          "span",
          "bench-last-batch-title",
          "Last callback in window: " +
            fmtInt(lo) +
            " ops · " +
            fmtFixed(lus, 1) +
            " µs/op · " +
            fmtFixed(lms, 2) +
            " ms total"
        )
      );
      card.appendChild(last);

      grid.appendChild(card);
    });
    root.appendChild(grid);
  }

  window.demoApplyResults = function (obj) {
    renderBenchVisual(obj);
    var elPre = document.getElementById("resultsDump");
    if (!elPre) return;
    try {
      elPre.textContent = JSON.stringify(obj || {}, null, 2);
    } catch (e) {
      elPre.textContent = String(obj);
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
