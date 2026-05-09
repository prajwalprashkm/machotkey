(function () {
    var FIELDS = [
        { section: "Performance", key: "TARGET_FPS", label: "Target FPS", step: 1, min: 1, max: 240 },
        { section: "Performance", key: "MAX_LATENCY", label: "Max latency (frames)", step: 1, min: 0, max: 120 },
        { section: "Performance", key: "MAX_CPU", label: "Max CPU %", step: 1, min: 1, max: 100 },
        { section: "Performance", key: "MAX_RAM", label: "Max RAM (MB)", step: 64, min: 256, max: 65536 },

        { section: "Timing", key: "RESTART_DELAY", label: "Restart delay (ms)", step: 50, min: 0, max: 60000 },
        { section: "Timing", key: "HOLD_ROD_CAST_DURATION", label: "Hold rod cast (ms)", step: 10, min: 0, max: 10000 },
        { section: "Timing", key: "WAIT_FOR_BOBBER_DELAY", label: "Wait bobber delay", step: 1, min: 0, max: 200 },
        { section: "Timing", key: "BAIT_DELAY", label: "Bait delay (ms)", step: 10, min: 0, max: 10000 },
        { section: "Timing", key: "SHAKE_FAILSAFE", label: "Shake failsafe (ms)", step: 100, min: 1000, max: 120000 },
        { section: "Timing", key: "CLICK_SCAN_DELAY", label: "Click scan delay (ms)", step: 1, min: 0, max: 500 },
        { section: "Timing", key: "SCAN_DELAY", label: "Scan delay (ms)", step: 1, min: 0, max: 500 },
        { section: "Timing", key: "SIDE_DELAY", label: "Side delay (ms)", step: 10, min: 0, max: 5000 },

        { section: "Tolerances", key: "CLICK_SHAKE_COLOR_TOLERANCE", label: "Click shake color tol.", step: 1, min: 0, max: 64 },
        { section: "Tolerances", key: "FISH_BAR_COLOR_TOLERANCE", label: "Fish bar color tol.", step: 1, min: 0, max: 64 },
        { section: "Tolerances", key: "WHITE_BAR_COLOR_TOLERANCE", label: "White bar color tol.", step: 1, min: 0, max: 64 },
        { section: "Tolerances", key: "ARROW_COLOR_TOLERANCE", label: "Arrow color tol.", step: 1, min: 0, max: 64 },

        { section: "Gameplay", key: "CONTROL", label: "Control", step: 0.01, min: 0, max: 1 },
        { section: "Gameplay", key: "SIDE_BAR_RATIO", label: "Side bar ratio", step: 0.01, min: 0, max: 1 },
        { section: "Gameplay", key: "NOT_FOUND_THRESHOLD", label: "Not found threshold", step: 1, min: 1, max: 200 },

        { section: "Stable / unstable", key: "STABLE_RIGHT_MULTIPLIER", label: "Stable right ×", step: 0.01, min: 0.1, max: 10 },
        { section: "Stable / unstable", key: "STABLE_RIGHT_DIVISION", label: "Stable right ÷", step: 0.01, min: 0.1, max: 10 },
        { section: "Stable / unstable", key: "STABLE_LEFT_MULTIPLIER", label: "Stable left ×", step: 0.01, min: 0.1, max: 10 },
        { section: "Stable / unstable", key: "STABLE_LEFT_DIVISION", label: "Stable left ÷", step: 0.01, min: 0.1, max: 10 },
        { section: "Stable / unstable", key: "UNSTABLE_RIGHT_MULTIPLIER", label: "Unstable right ×", step: 0.01, min: 0.1, max: 10 },
        { section: "Stable / unstable", key: "UNSTABLE_RIGHT_DIVISION", label: "Unstable right ÷", step: 0.01, min: 0.1, max: 10 },
        { section: "Stable / unstable", key: "UNSTABLE_LEFT_MULTIPLIER", label: "Unstable left ×", step: 0.01, min: 0.1, max: 10 },
        { section: "Stable / unstable", key: "UNSTABLE_LEFT_DIVISION", label: "Unstable left ÷", step: 0.01, min: 0.1, max: 10 },
        { section: "Stable / unstable", key: "RIGHT_ANKLE_BREAK_MULTIPLIER", label: "Right ankle break ×", step: 0.01, min: 0, max: 2 },
        { section: "Stable / unstable", key: "LEFT_ANKLE_BREAK_MULTIPLIER", label: "Left ankle break ×", step: 0.01, min: 0, max: 2 },
    ];

    var BOOLS = [
        { section: "Modes & flags", key: "CLICK_SHAKE_MODE", label: "Click shake mode" },
        { section: "Modes & flags", key: "POLARIS_MODE", label: "Polaris mode" },
        { section: "Modes & flags", key: "SERA_MODE", label: "Sera mode" },
        { section: "Modes & flags", key: "REAVER_MODE", label: "Reaver mode" },
        { section: "Modes & flags", key: "CASTBOUND_MODE", label: "Castbound mode" },
    ];

    var COLORS = [
        { section: "Colors (RGB)", prefix: "SHAKE", label: "Shake" },
        { section: "Colors (RGB)", prefix: "FISH", label: "Fish" },
        { section: "Colors (RGB)", prefix: "BAR", label: "Bar" },
        { section: "Colors (RGB)", prefix: "ARROW", label: "Arrow" },
    ];

    function bySection(items) {
        var m = {};
        items.forEach(function (it) {
            if (!m[it.section]) m[it.section] = [];
            m[it.section].push(it);
        });
        return m;
    }

    function buildForm() {
        var root = document.getElementById("formRoot");
        root.innerHTML = "";

        function card(title, inner) {
            var c = document.createElement("div");
            c.className = "card";
            c.innerHTML = "<h2>" + title + "</h2>" + inner;
            return c;
        }

        var secMap = bySection(FIELDS);
        Object.keys(secMap).forEach(function (sec) {
            var html = secMap[sec]
                .map(function (f) {
                    return (
                        '<div class="field" data-key="' +
                        f.key +
                        '">' +
                        '<label><span class="key">' +
                        f.key +
                        "</span></label>" +
                        '<input type="number" data-k="' +
                        f.key +
                        '" step="' +
                        f.step +
                        '" min="' +
                        f.min +
                        '" max="' +
                        f.max +
                        '" />' +
                        "</div>"
                    );
                })
                .join("");
            root.appendChild(card(sec, html));
        });

        var boolHtml = BOOLS.map(function (b) {
            return (
                '<div class="toggle"><input type="checkbox" data-k="' +
                b.key +
                '" id="cb_' +
                b.key +
                '" /><label for="cb_' +
                b.key +
                '">' +
                b.label +
                "</label></div>"
            );
        }).join("");
        root.appendChild(card("Modes & flags", boolHtml));

        var colorHtml = COLORS.map(function (c) {
            var p = c.prefix;
            return (
                '<div class="field"><label>' +
                c.label +
                "</label>" +
                '<div class="rgb-row">' +
                '<input type="number" data-k="' +
                p +
                '_R" min="0" max="255" placeholder="R" />' +
                '<input type="number" data-k="' +
                p +
                '_G" min="0" max="255" placeholder="G" />' +
                '<input type="number" data-k="' +
                p +
                '_B" min="0" max="255" placeholder="B" />' +
                "</div></div>"
            );
        }).join("");
        root.appendChild(card("Colors (RGB)", colorHtml));
    }

    function apply(data) {
        data = data || {};
        document.querySelectorAll("[data-k]").forEach(function (el) {
            var k = el.getAttribute("data-k");
            if (!(k in data)) return;
            var v = data[k];
            if (el.type === "checkbox") el.checked = !!v;
            else el.value = String(v);
        });
    }

    function collect() {
        var lines = [];
        document.querySelectorAll("input[data-k]").forEach(function (el) {
            var k = el.getAttribute("data-k");
            if (el.type === "checkbox") lines.push(k + "=" + (el.checked ? "true" : "false"));
            else {
                var n = parseFloat(el.value);
                if (!isNaN(n)) lines.push(k + "=" + n);
            }
        });
        return lines.join("\n");
    }

    window.macroConfigApply = function (data) {
        apply(data);
    };

    window.macroConfigToast = function (msg, ok) {
        var t = document.getElementById("toast");
        t.textContent = msg;
        t.className = "toast show " + (ok ? "ok" : "err");
        clearTimeout(window._toastT);
        window._toastT = setTimeout(function () {
            t.className = "toast";
        }, 3200);
    };

    document.getElementById("btnSave").addEventListener("click", function () {
        if (window.macroUI && window.macroUI.emit) {
            window.macroUI.emit("config_save", collect());
        }
    });

    document.getElementById("btnReset").addEventListener("click", function () {
        if (window.macroUI && window.macroUI.emit) {
            window.macroUI.emit("config_reset", "");
        }
    });

    buildForm();

    function emitReady() {
        if (window.macroUI && window.macroUI.emit) {
            window.macroUI.emit("ui_ready", "");
            return;
        }
        setTimeout(emitReady, 16);
    }

    if (document.readyState === "loading") {
        document.addEventListener("DOMContentLoaded", emitReady);
    } else {
        emitReady();
    }
})();

