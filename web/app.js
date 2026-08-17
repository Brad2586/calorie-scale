// Calorie Scale web app
// Reads weight + photo from the ESP32-CAM, identifies the food with the
// Gemini API, and computes total calories = kcal/g x grams.

const $ = (id) => document.getElementById(id);

// ---------- Settings (persisted in localStorage) ----------

const settings = {
  espIp: localStorage.getItem("espIp") || "",
  apiKey: localStorage.getItem("apiKey") || "",
  model: localStorage.getItem("model") || "gemini-2.5-flash",
  useFlash: localStorage.getItem("useFlash") === "1",
};

function espUrl(path) {
  let host = settings.espIp.trim().replace(/^https?:\/\//, "").replace(/\/+$/, "");
  return `http://${host}${path}`;
}

function loadSettingsIntoForm() {
  $("espIp").value = settings.espIp;
  $("apiKey").value = settings.apiKey;
  $("model").value = settings.model;
  $("useFlash").checked = settings.useFlash;
}

$("settingsBtn").addEventListener("click", () => {
  $("settingsPanel").classList.toggle("hidden");
});

$("saveSettings").addEventListener("click", () => {
  settings.espIp = $("espIp").value.trim();
  settings.apiKey = $("apiKey").value.trim();
  settings.model = $("model").value.trim() || "gemini-2.5-flash";
  settings.useFlash = $("useFlash").checked;
  localStorage.setItem("espIp", settings.espIp);
  localStorage.setItem("apiKey", settings.apiKey);
  localStorage.setItem("model", settings.model);
  localStorage.setItem("useFlash", settings.useFlash ? "1" : "0");
  $("settingsStatus").textContent = "Saved.";
  setTimeout(() => ($("settingsStatus").textContent = ""), 2000);
});

// ---------- Live weight polling ----------

let currentGrams = null;

async function pollWeight() {
  if (!settings.espIp) {
    setConnection(false);
    return;
  }
  try {
    const res = await fetch(espUrl("/weight"), { signal: AbortSignal.timeout(3000) });
    const data = await res.json();
    if (typeof data.grams === "number") {
      currentGrams = data.grams;
      $("weight").textContent = data.grams.toFixed(1);
      setConnection(true);
    } else {
      throw new Error(data.error || "bad response");
    }
  } catch {
    currentGrams = null;
    $("weight").textContent = "--";
    setConnection(false);
  }
}

function setConnection(online) {
  const el = $("connStatus");
  el.textContent = online ? "connected" : "offline";
  el.className = "conn " + (online ? "online" : "offline");
}

setInterval(pollWeight, 800);
pollWeight();

$("tareBtn").addEventListener("click", async () => {
  try {
    await fetch(espUrl("/tare"), { signal: AbortSignal.timeout(4000) });
  } catch {
    alert("Could not reach the scale. Check the ESP32 address in Settings.");
  }
});

// ---------- Food identification with Gemini ----------

const PROMPT = `You are a nutrition assistant. The photo comes from a camera mounted above a kitchen scale.
Identify the primary food item on the scale and estimate its calorie density as shown/prepared.
Respond with JSON only, exactly this shape:
{"food": "<short lowercase name, or 'none' if no food is visible>", "kcal_per_100g": <number>, "confidence": <number between 0 and 1>}`;

async function capturePhoto() {
  const url = espUrl("/photo") + (settings.useFlash ? "?flash=1" : "");
  const res = await fetch(url, { signal: AbortSignal.timeout(15000) });
  if (!res.ok) throw new Error("Camera capture failed");
  return await res.blob();
}

function blobToBase64(blob) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(reader.result.split(",")[1]);
    reader.onerror = reject;
    reader.readAsDataURL(blob);
  });
}

async function identifyFood(base64Jpeg) {
  const url = `https://generativelanguage.googleapis.com/v1beta/models/${settings.model}:generateContent`;
  const body = {
    contents: [{
      parts: [
        { text: PROMPT },
        { inline_data: { mime_type: "image/jpeg", data: base64Jpeg } },
      ],
    }],
    generationConfig: { response_mime_type: "application/json" },
  };
  const res = await fetch(url, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      "x-goog-api-key": settings.apiKey,
    },
    body: JSON.stringify(body),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error(err.error?.message || `Gemini request failed (${res.status})`);
  }
  const data = await res.json();
  const text = data.candidates?.[0]?.content?.parts?.[0]?.text;
  if (!text) throw new Error("Gemini returned no answer");
  return JSON.parse(text);
}

// ---------- Analyze flow ----------

let lastResult = null;

function showPhoto(blob) {
  $("photo").src = URL.createObjectURL(blob);
  $("photoWrap").classList.remove("hidden");
}

$("previewBtn").addEventListener("click", async () => {
  const status = $("analyzeStatus");
  if (!settings.espIp) {
    $("settingsPanel").classList.remove("hidden");
    status.textContent = "Enter the ESP32 address in Settings first.";
    return;
  }
  const btn = $("previewBtn");
  btn.disabled = true;
  status.textContent = "Taking photo...";
  try {
    showPhoto(await capturePhoto());
    status.textContent = "";
  } catch (e) {
    status.textContent = "Error: " + e.message;
  } finally {
    btn.disabled = false;
  }
});

$("analyzeBtn").addEventListener("click", async () => {
  const status = $("analyzeStatus");
  const btn = $("analyzeBtn");

  if (!settings.espIp || !settings.apiKey) {
    $("settingsPanel").classList.remove("hidden");
    status.textContent = "Enter the ESP32 address and your Gemini API key in Settings first.";
    return;
  }
  if (currentGrams === null) {
    status.textContent = "Scale is offline - cannot read the weight.";
    return;
  }

  const grams = currentGrams;
  btn.disabled = true;
  $("result").classList.add("hidden");

  try {
    status.textContent = "Taking photo...";
    const blob = await capturePhoto();
    showPhoto(blob);

    status.textContent = "Identifying food...";
    const info = await identifyFood(await blobToBase64(blob));

    if (!info.food || info.food === "none") {
      status.textContent = "No food detected on the scale. Try repositioning it or use the flash.";
      return;
    }

    const totalKcal = (info.kcal_per_100g / 100) * grams;
    lastResult = { food: info.food, grams, kcalPer100: info.kcal_per_100g, totalKcal };

    $("foodName").textContent = info.food;
    $("rWeight").textContent = grams.toFixed(1) + " g";
    $("rPer100").textContent = Math.round(info.kcal_per_100g) + " kcal";
    $("rConf").textContent = Math.round((info.confidence ?? 0) * 100) + "%";
    $("rTotal").textContent = Math.round(totalKcal) + " kcal";
    $("result").classList.remove("hidden");
    status.textContent = "";
  } catch (e) {
    status.textContent = "Error: " + e.message;
  } finally {
    btn.disabled = false;
  }
});

// ---------- Session log ----------

let log = JSON.parse(localStorage.getItem("sessionLog") || "[]");

function renderLog() {
  const ul = $("log");
  ul.innerHTML = "";
  let total = 0;
  for (const item of log) {
    total += item.totalKcal;
    const li = document.createElement("li");
    li.innerHTML = `<span>${item.food}<span class="muted">${item.grams.toFixed(0)} g</span></span>` +
                   `<strong>${Math.round(item.totalKcal)} kcal</strong>`;
    ul.appendChild(li);
  }
  $("sessionTotal").textContent = Math.round(total);
}

$("addBtn").addEventListener("click", () => {
  if (!lastResult) return;
  log.push(lastResult);
  localStorage.setItem("sessionLog", JSON.stringify(log));
  renderLog();
});

$("clearLog").addEventListener("click", () => {
  log = [];
  localStorage.setItem("sessionLog", "[]");
  renderLog();
});

loadSettingsIntoForm();
renderLog();
