(function () {
  function emit(name, payload) {
    if (window.macroUI && window.macroUI.emit) {
      window.macroUI.emit(name, payload || "");
    }
  }

  function collectPayload() {
    var ids = [
      "TARGET_FPS",
      "SCAN_TOLERANCE",
      "TARGET_R",
      "TARGET_G",
      "TARGET_B",
      "AUTOMATION_INTERVAL_MS",
      "CLICK_X_RATIO",
      "CLICK_Y_RATIO",
    ];

    return ids
      .map(function (id) {
        var el = document.getElementById(id);
        var value = parseFloat(el.value);
        if (isNaN(value)) {
          return null;
        }
        return id + "=" + value;
      })
      .filter(Boolean)
      .join("\n");
  }

  function setText(id, text) {
    var el = document.getElementById(id);
    if (el) {
      el.textContent = text;
    }
  }

  var arenaEl = null;
  var squareEl = null;
  var targetX = 14;
  var targetY = 18;
  var velocityX = 3.2;
  var velocityY = 2.4;
  var lastTargetEmitAt = 0;

  function emitTargetGeometry() {
    if (!arenaEl || !squareEl) {
      return;
    }
    var arenaRect = arenaEl.getBoundingClientRect();
    var squareRect = squareEl.getBoundingClientRect();

    var chromeY = window.outerHeight - window.innerHeight;
    var chromeX = Math.max(0, (window.outerWidth - window.innerWidth) / 2);
    var screenBaseX = window.screenX + chromeX;
    var screenBaseY = window.screenY + chromeY;

    var centerX = Math.round(screenBaseX + squareRect.left + squareRect.width / 2);
    var centerY = Math.round(screenBaseY + squareRect.top + squareRect.height / 2);

    var payload = [
      "cx=" + centerX,
      "cy=" + centerY,
      "scan_x=" + Math.round(screenBaseX + arenaRect.left),
      "scan_y=" + Math.round(screenBaseY + arenaRect.top),
      "scan_w=" + Math.round(arenaRect.width),
      "scan_h=" + Math.round(arenaRect.height),
    ].join("\n");
    emit("ui_target", payload);
  }

  function animateTarget() {
    if (!arenaEl || !squareEl) {
      requestAnimationFrame(animateTarget);
      return;
    }

    var maxX = Math.max(0, arenaEl.clientWidth - squareEl.offsetWidth);
    var maxY = Math.max(0, arenaEl.clientHeight - squareEl.offsetHeight);

    targetX += velocityX;
    targetY += velocityY;

    if (targetX <= 0 || targetX >= maxX) {
      targetX = Math.max(0, Math.min(maxX, targetX));
      velocityX = -velocityX;
    }
    if (targetY <= 0 || targetY >= maxY) {
      targetY = Math.max(0, Math.min(maxY, targetY));
      velocityY = -velocityY;
    }

    squareEl.style.transform = "translate(" + Math.round(targetX) + "px, " + Math.round(targetY) + "px)";

    var now = performance.now();
    if (now - lastTargetEmitAt >= 45) {
      emitTargetGeometry();
      lastTargetEmitAt = now;
    }

    requestAnimationFrame(animateTarget);
  }

  window.demoApply = function (data) {
    data = data || {};
    [
      "TARGET_FPS",
      "SCAN_TOLERANCE",
      "TARGET_R",
      "TARGET_G",
      "TARGET_B",
      "AUTOMATION_INTERVAL_MS",
      "CLICK_X_RATIO",
      "CLICK_Y_RATIO",
    ].forEach(function (key) {
      if (data[key] !== undefined) {
        var el = document.getElementById(key);
        if (el) {
          el.value = String(data[key]);
        }
      }
    });

    if (data.screen_w && data.screen_h) {
      setText("rtScreen", data.screen_w + "x" + data.screen_h);
    }

    setText(
      "rtStatus",
      data.paused ? "Paused" : (data.automation_enabled ? "Automation on" : "Running")
    );
  };

  window.demoUpdateRuntime = function (data) {
    data = data || {};

    if (data.paused !== undefined || data.automation_enabled !== undefined) {
      setText(
        "rtStatus",
        data.paused ? "Paused" : (data.automation_enabled ? "Automation on" : "Running")
      );
    }
    if (data.target_seen !== undefined) {
      setText("rtSeen", data.target_seen ? "Yes" : "No");
    }
    if (data.fps !== undefined) {
      setText("rtFps", data.fps.toFixed(1));
    }
    if (data.latency_ms !== undefined) {
      setText("rtLatency", data.latency_ms.toFixed(3) + " ms");
    }
    if (data.dropped !== undefined) {
      setText("rtDropped", String(data.dropped));
    }
    if (data.cpu !== undefined) {
      setText("rtCpu", data.cpu.toFixed(2) + "%");
    }
    if (data.ram !== undefined) {
      setText("rtRam", data.ram.toFixed(2) + " MB");
    }
    if (data.clicks_per_sec !== undefined) {
      setText("rtCps", data.clicks_per_sec.toFixed(1));
    }
  };

  window.demoToast = function (message) {
    var toast = document.getElementById("toast");
    toast.textContent = message;
    toast.className = "toast show";
    clearTimeout(window.__demoToastT);
    window.__demoToastT = setTimeout(function () {
      toast.className = "toast";
    }, 2800);
  };

  document.getElementById("btnPause").addEventListener("click", function () {
    emit("toggle_pause", "");
  });

  document.getElementById("btnAuto").addEventListener("click", function () {
    emit("toggle_automation", "");
  });

  document.getElementById("btnSnap").addEventListener("click", function () {
    emit("snapshot", "");
  });

  document.getElementById("btnSave").addEventListener("click", function () {
    emit("config_save", collectPayload());
  });

  function emitReady() {
    if (window.macroUI && window.macroUI.emit) {
      emit("ui_ready", "");
      return;
    }
    setTimeout(emitReady, 16);
  }

  arenaEl = document.getElementById("targetArena");
  squareEl = document.getElementById("targetSquare");
  requestAnimationFrame(animateTarget);

  if (document.readyState === "loading") {
    document.addEventListener("DOMContentLoaded", emitReady);
  } else {
    emitReady();
  }
})();
