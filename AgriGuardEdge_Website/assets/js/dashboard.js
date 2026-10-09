/* AgriGuard Edge dashboard — panels per docs/architechture.md §9.
 * All data comes from the edge-ingest API (/api/events /api/summary
 * /api/probes /api/energy). No legacy endpoints. Refresh: 15 s.
 */
let soilTraceChart = null;
let conservationChart = null;
let flowChart = null;
let cusumChart = null;
let energyChart = null;

const SEV_BADGE = { HIGH: "bg-danger", MED: "bg-warning", LOW: "bg-info", INFO: "bg-secondary", NONE: "bg-success" };

async function getJSON(url) {
  const r = await fetch(url);
  if (!r.ok) throw new Error(url + " -> " + r.status);
  return r.json();
}

function renderSoilTrace(summaries) {
  const el = document.querySelector("#soilTraceChart");
  if (!el) return;
  const rows = (summaries || []).slice().reverse();
  const pts = rows.map((s) => ({
    x: new Date(s.received_at * 1000).getTime(),
    y: s.avg_moisture,
  }));
  // Diurnal baseline band from the summaries themselves: mean ± 2σ drawn as
  // two dashed edge lines. Plain line series only — the vendored ApexCharts
  // build predates rangeArea support and crashes on that type.
  const series = [{ name: "soil %", data: pts }];
  if (rows.length > 3) {
    const vals = rows.map((s) => s.avg_moisture);
    const mean = vals.reduce((a, b) => a + b, 0) / vals.length;
    const sd = Math.sqrt(vals.reduce((a, b) => a + (b - mean) ** 2, 0) / vals.length) || 1;
    const hi = +(mean + 2 * sd).toFixed(2);
    const lo = +(mean - 2 * sd).toFixed(2);
    series.push(
      { name: "baseline +2σ", data: pts.map((p) => ({ x: p.x, y: hi })) },
      { name: "baseline −2σ", data: pts.map((p) => ({ x: p.x, y: lo })) }
    );
  }
  const opts = {
    series,
    chart: { height: 300, type: "line", toolbar: { show: true } },
    xaxis: { type: "datetime" },
    yaxis: { title: { text: "%" } },
    // Arrays built to match the series count exactly — a longer array than
    // series is another known way to upset old ApexCharts builds.
    stroke: {
      width: series.map((_, i) => (i === 0 ? 3 : 1)),
      dashArray: series.map((_, i) => (i === 0 ? 0 : 5)),
    },
    colors: series.map((_, i) => (i === 0 ? "#0d6efd" : "#adb5bd")),
  };
  if (soilTraceChart) soilTraceChart.updateOptions(opts);
  else { soilTraceChart = new ApexCharts(el, opts); soilTraceChart.render(); }
}

function renderConservation(probes) {
  const el = document.querySelector("#conservationChart");
  if (!el) return;
  const rows = (probes || []).slice().reverse().slice(-8);
  const cats = rows.map((p) => p.verdict + " #" + p.received_at.toFixed(0).slice(-4));
  const retained = rows.map((p) => +(p.V_soil_delta_ml || 0));
  const resid = rows.map((p) => +((p.V_delivered_ml || 0) - (p.V_soil_delta_ml || 0)));
  const colors = rows.map((p) =>
    /LEAK|BLOCKAGE|DRIFT/.test(p.verdict || "") ? "#dc3545" : "#198754");
  const opts = {
    series: [
      { name: "V_soil retained (mL)", data: retained },
      { name: "V_residual unaccounted (mL)", data: resid },
    ],
    chart: { height: 300, type: "bar", stacked: true, toolbar: { show: true } },
    xaxis: { categories: cats },
    colors: ["#0d6efd", "#dc3545"],
    plotOptions: { bar: { horizontal: true } },
  };
  if (conservationChart) conservationChart.updateOptions(opts);
  else { conservationChart = new ApexCharts(el, opts); conservationChart.render(); }
}

function renderFlow(events) {
  const el = document.querySelector("#flowChart");
  if (!el) return;
  // Flow evidence rides on every alert/probe_result — sparse but real.
  const pts = (events || [])
    .slice().reverse()
    .filter((e) => e.evidence && e.evidence.flow_rate_lpm !== undefined)
    .map((e) => ({
      x: new Date(e.received_at * 1000).getTime(),
      y: e.evidence.flow_rate_lpm || 0,
    }));
  const opts = {
    series: [{ name: "L/min", data: pts.length ? pts : [{ x: Date.now(), y: 0 }] }],
    chart: { height: 300, type: "area", toolbar: { show: true } },
    xaxis: { type: "datetime" },
    yaxis: { title: { text: "L/min" }, min: 0 },
    annotations: { yaxis: [{ y: 0.3, borderColor: "#dc3545", label: { text: "leak ε" } }] },
  };
  if (flowChart) flowChart.updateOptions(opts);
  else { flowChart = new ApexCharts(el, opts); flowChart.render(); }
}

function renderCusum(events) {
  const el = document.querySelector("#cusumChart");
  if (!el) return;
  const rows = (events || []).slice().reverse().slice(-40);
  const lvl = { NORMAL: 0, SUSPECT: 1, ALARM: 2 };
  const pts = rows.map((e) => ({
    x: new Date(e.received_at * 1000).getTime(),
    y: lvl[e.state] ?? 0,
  }));
  const opts = {
    series: [{ name: "state", data: pts.length ? pts : [{ x: Date.now(), y: 0 }] }],
    chart: { height: 260, type: "line", toolbar: { show: false } },
    stroke: { curve: "stepline", width: 3 },
    yaxis: { min: 0, max: 2, tickAmount: 2 },
    xaxis: { type: "datetime" },
  };
  if (cusumChart) cusumChart.updateOptions(opts);
  else { cusumChart = new ApexCharts(el, opts); cusumChart.render(); }
}

function renderEnergy(summaries, energy) {
  const el = document.querySelector("#energyChart");
  if (!el) return;
  const sent = (energy || []).reduce((a, e) => a + (e.tx_count || 0), 0);
  const generated = Math.max((summaries || []).length * 15, sent); // ~15 evals/summary
  const opts = {
    series: [{ name: "messages", data: [generated, sent] }],
    chart: { height: 260, type: "bar", toolbar: { show: false } },
    xaxis: { categories: ["readings generated", "messages sent"] },
    colors: ["#6c757d", "#198754"],
    plotOptions: { bar: { horizontal: true, distributed: true } },
    legend: { show: false },
  };
  if (energyChart) energyChart.updateOptions(opts);
  else { energyChart = new ApexCharts(el, opts); energyChart.render(); }
  const note = document.querySelector("#energyNote");
  if (note) {
    const saved = generated > 0 ? (100 * (1 - sent / generated)).toFixed(1) : "0.0";
    note.textContent = `${generated} readings → ${sent} messages · ~${saved}% suppressed (target ≥90%, §14.1)`;
  }
}

function renderAlerts(events) {
  const box = document.querySelector("#alertStream");
  if (!box) return;
  box.innerHTML = "";
  const rows = (events || []).slice(0, 10);
  if (!rows.length) {
    box.innerHTML = '<p class="text-muted small">No alerts yet — node is inside its baseline band.</p>';
    return;
  }
  for (const e of rows) {
    const sev = e.severity || "INFO";
    const card = document.createElement("div");
    card.className = "border rounded p-2";
    const ev = e.evidence && typeof e.evidence === "object" ? e.evidence : {};
    const cons =
      ev.V_delivered_ml !== undefined
        ? `V_del ${ev.V_delivered_ml} mL · V_soil ${ev.V_soil_delta_ml} mL · V_ET ${ev.V_et_ml} mL · ratio ${ev.residual_ratio}`
        : ev.scenario
          ? `scenario marker "${ev.scenario}" — timeline bookmark, not an alert`
          : `z ${ev.z_soil ?? "?"} · cusum↓ ${ev.cusum_down ?? "?"} · deficit ${ev.deficit ?? "?"}`;
    card.innerHTML =
      `<span class="badge ${SEV_BADGE[sev] || "bg-secondary"}">${sev}</span> ` +
      `<strong>${e.event}</strong> ` +
      `<span class="text-muted small">${new Date(e.received_at * 1000).toLocaleString()} · ${e.state || ""} · ${e.ack_state || "UNACK"}</span>` +
      `<details class="small mt-1"><summary>evidence</summary><code>${cons}<br>${JSON.stringify(ev)}</code></details>` +
      (e.ack_state !== "ACK" ? `<button class="btn btn-sm btn-outline-secondary mt-1" data-ack="${e.id}">acknowledge</button>` : "");
    box.appendChild(card);
  }
  box.querySelectorAll("[data-ack]").forEach((b) =>
    b.addEventListener("click", async () => {
      await fetch("/api/ack", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ id: b.dataset.ack }),
      });
      refresh();
    })
  );
}

function renderState(events) {
  const latest = (events || [])[0];
  const state = (latest && latest.state) || "NORMAL";
  const badge = document.querySelector("#stateBadge");
  if (badge) {
    badge.textContent = state;
    badge.className = "badge " + (state === "ALARM" ? "bg-danger" : state === "SUSPECT" ? "bg-warning" : "bg-success");
  }
  const live = document.querySelector("#liveStateBadge");
  if (live && latest) {
    live.textContent = latest.event || "OK";
    live.className = "badge " + (SEV_BADGE[latest.severity] || "bg-success");
  }
  if (latest && latest.evidence && typeof latest.evidence === "object") {
    const ev = latest.evidence;
    const set = (id, v) => { const n = document.querySelector(id); if (n) n.textContent = v; };
    set("#cusumUp", (ev.cusum_up ?? 0).toFixed?.(2) ?? ev.cusum_up);
    set("#cusumDown", (ev.cusum_down ?? 0).toFixed?.(2) ?? ev.cusum_down);
    set("#zSoil", ev.z_soil ?? "—");
    set("#deficit", ev.deficit ?? "—");
  }
}

async function refresh() {
  try {
    const [events, summaries, probes, energy] = await Promise.all([
      getJSON("/api/events").catch(() => []),
      getJSON("/api/summary").catch(() => []),
      getJSON("/api/probes").catch(() => []),
      getJSON("/api/energy").catch(() => []),
    ]);
    renderSoilTrace(summaries);
    renderConservation(probes);
    renderFlow(events);
    renderCusum(events);
    renderEnergy(summaries, energy);
    renderAlerts(events);
    renderState(events);
  } catch (err) {
    console.error("dashboard refresh failed:", err);
  }
}

document.addEventListener("DOMContentLoaded", () => {
  refresh();
  setInterval(refresh, 15000);
  document.querySelectorAll("#scenarioBtns button[data-scenario]").forEach((b) =>
    b.addEventListener("click", async () => {
      await fetch("/api/scenario", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ scenario: b.dataset.scenario }),
      });
      refresh();
      setTimeout(refresh, 600); // second pass once the rows commit
    })
  );
  const reset = document.querySelector("#resetDemo");
  if (reset) reset.addEventListener("click", async () => {
    await fetch("/api/demo/reset", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: "{}",
    });
    refresh();
    setTimeout(refresh, 600);
  });
});
