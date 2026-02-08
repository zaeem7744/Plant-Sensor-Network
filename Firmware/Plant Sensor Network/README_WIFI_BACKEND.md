# Plant Sensor Network – Wi‑Fi Backend & Dashboard Setup

This document explains how to run the project end‑to‑end using Wi‑Fi (no USB serial) and how someone else can run the same setup on their own machine.

It assumes three parts:

1. **Firmware (ESP32/S3)** – reads all sensors and sends JSON over Wi‑Fi.
2. **Python backend** – receives JSON via HTTP, stores it in SQLite, and serves an HTTP API.
3. **Web dashboard** – calls the backend API to show real‑time and historical graphs.

---

## Folder layout (recommended)

Adapt this to your actual repo, but the examples assume:

- `Firmware/Plant Sensor Network/` – ESP32 firmware (this project)
- `backend/` – Python backend service (FastAPI + SQLite)

This file (`README_WIFI_BACKEND.md`) lives in `Firmware/Plant Sensor Network/`.

---

## 1. ESP32 firmware: Wi‑Fi + HTTP JSON

### 1.1. Config header

Create a `config.h` in your firmware folder and avoid hard‑coding personal details in the main code.

Example `config.h`:

```cpp
#pragma once

// WiFi credentials
#define WIFI_SSID     "CHANGE_ME_SSID"
#define WIFI_PASSWORD "CHANGE_ME_PASSWORD"

// Backend ingest URL (Python server on local network)
// Example: http://192.168.0.23:8000/api/ingest
#define BACKEND_URL   "http://192.168.0.23:8000/api/ingest"
```

In your main firmware file (the one with `setup()` / `loop()`), include it:

```cpp
#include "config.h"
```

Each new user only needs to edit `config.h` with:

- Their Wi‑Fi SSID and password.
- The IP and port of their backend machine.

### 1.2. Wi‑Fi setup (high level)

In `setup()`:

1. Start serial (for debugging if desired).
2. Connect to Wi‑Fi using `WIFI_SSID` / `WIFI_PASSWORD` from `config.h`.
3. Once connected, continue with I2C and sensor initialisation.

Pseudocode:

```cpp
WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
while (WiFi.status() != WL_CONNECTED) {
  delay(500);
}
// Wi‑Fi connected – continue initialising sensors
```

### 1.3. Sending sensor data as JSON

At the end of each measurement cycle in `loop()`, after reading all sensors:

1. Build a JSON object (e.g. using ArduinoJson) containing all readings.
2. Send it to `BACKEND_URL` via HTTP POST.

High‑level example:

```cpp
StaticJsonDocument<1024> doc;

// Example fields – adapt to your actual variables
// doc["ts_ms"] = millis();
// doc["MS8607"]["T"]  = temp.temperature;
// doc["MS8607"]["RH"] = humidity.relative_humidity;
// doc["MS8607"]["P"]  = pressure.pressure;
// doc["MULTIGAS"]["O3"]["value"] = concO3;
// doc["MULTIGAS"]["O3"]["unit"]  = "ppm";
// ... etc ...

if (WiFi.status() == WL_CONNECTED) {
  HTTPClient http;
  http.begin(BACKEND_URL);
  http.addHeader("Content-Type", "application/json");

  String payload;
  serializeJson(doc, payload);

  int httpCode = http.POST(payload);
  // optional: check httpCode for success
  http.end();
}
```

The existing serial text output can stay for debugging, but the backend will rely on these JSON POSTs.

---

## 2. Python backend (FastAPI + SQLite)

The backend runs on a PC in the same network as the ESP32. It:

- Listens for HTTP POSTs from the ESP32 at `/api/ingest`.
- Stores every reading in a local SQLite database (`sensors.db`).
- Provides HTTP endpoints for the dashboard, such as:
  - `GET /api/latest` – latest value per sensor/field.
  - `GET /api/history` – historical time series.
  - `GET /api/status` – sensor online/offline.

### 2.1. Setup steps

In a folder like `backend/`:

1. Create a virtual environment (optional but recommended).
2. Install dependencies:

   ```bash
   pip install fastapi uvicorn[standard] pydantic
   ```

3. Create a `main.py` (skeleton) that:
   - Opens/creates `sensors.db`.
   - Defines `/api/ingest` to accept JSON and write rows into `measurements`.
   - Defines `/api/latest` and `/api/status`.

4. Run the backend:

   ```bash
   uvicorn main:app --reload --host 0.0.0.0 --port 8000
   ```

5. Note your PC’s local IP (e.g. `192.168.0.23`) and set `BACKEND_URL` in `config.h` accordingly.

### 2.2. Database schema (simple)

Use a single `measurements` table to keep things flexible:

```sql
CREATE TABLE IF NOT EXISTS measurements (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts DATETIME NOT NULL,
  sensor_name TEXT NOT NULL,
  field TEXT NOT NULL,
  value REAL NOT NULL,
  unit TEXT
);
```

Each POST from the ESP32 becomes multiple rows (one per metric).

---

## 3. Dashboard

You have two main options:

1. **Lovable web app** (recommended if you want a pure web UI)
   - The Lovable app runs in a browser.
   - It calls the backend API on `http://<PC_IP>:8000/...`.
   - It can show:
     - Real‑time tiles using `/api/latest`.
     - Line charts using `/api/history`.
     - Sensor online/offline status using `/api/status`.

2. **Python dashboard (Streamlit, Dash, etc.)**
   - Runs on the same machine as the backend.
   - Talks directly to `sensors.db` or to the HTTP API.
   - Good if you want to keep everything in Python.

For collaborators, Lovable or a simple JS/HTML front‑end calling the API is easiest to share.

---

## 4. Sharing the project with someone else

When someone else clones this project, they need to adjust only a few things:

1. **Firmware**
   - Edit `config.h`:
     - Set `WIFI_SSID` and `WIFI_PASSWORD` to their Wi‑Fi.
     - Set `BACKEND_URL` to `http://<their‑PC‑IP>:8000/api/ingest`.
   - Build and flash the ESP32.

2. **Backend**
   - Install Python and requirements.
   - Run `uvicorn main:app --host 0.0.0.0 --port 8000`.
   - Ensure their firewall allows inbound connections on that port (or change the port).

3. **Dashboard**
   - Open the Lovable app (or local dashboard) configured to talk to the backend’s base URL.

As long as the ESP32 and the PC are on the same network and the URLs match, the system works the same way on any machine.

---

## 5. Sampling rate and sensor status

|- **Sampling rate** is currently controlled in firmware by the delay at the end of `loop()` (e.g. `delay(2000);`).
|- You can later add a configuration endpoint (e.g. `/api/config`) in the backend and have the firmware periodically fetch a target sampling interval.
|- **Sensor online/offline** can be determined in the backend by checking how recent the last measurement for each sensor is (e.g. online if last reading < 10 seconds ago).

---

## 6. Memory & storage guide (sampling vs. database size)

This section gives you a rough idea of how much disk space the SQLite database (`sensors.db`) will use over time for a given sampling rate.

### 6.1. What counts as one "sample"?

The firmware sends a JSON payload every measurement cycle. The Python backend breaks this JSON into **rows** in the `measurements` table:

```sql
CREATE TABLE measurements (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  ts DATETIME NOT NULL,
  sensor_name TEXT NOT NULL,
  parameter TEXT NOT NULL,
  value REAL NOT NULL,
  unit TEXT
);
```

Each **row** is one sensor parameter at one point in time (for example `MS8607 / temperature_c = 26.5 C`).
A single measurement cycle inserts many rows (one for each parameter from all sensors).

### 6.2. Approximate rows per cycle

With the current firmware, a typical JSON payload contains roughly:

- MS8607: 3 parameters (temperature, humidity, pressure)
- BH1750: 1 parameter (lux)
- Alcohol: 1 parameter (ppm)
- CH4: 3 parameters (LEL %, module temp, error code)
- HCHO: 1 parameter (ppm)
- MULTIGAS: 5 parameters (H2S, O2, NH3, CO, O3)
- Soil EC+pH: 2 parameters (when online)

That is about **16–18 parameters per cycle**. To be safe, you can round this up to **20 rows per cycle**.

### 6.3. Default sampling rate

At the end of `loop()` the firmware waits before starting the next cycle:

```cpp
// in sensors.cpp
delay(2000);  // 2000 ms = 2 seconds between cycles
```

With `delay(2000)`:

- **Cycles per second**: `1 cycle / 2 s = 0.5 cycles/s`
- **Rows per second**: `0.5 cycles/s × ~20 rows/cycle ≈ 10 rows/s`

You can change the delay to trade off time resolution vs. database size.

### 6.4. Storage estimate per day

SQLite is compact, but an easy planning number is **~100 bytes per row** (including payload + internal overhead).

Using the default settings above:

- Rows per second: ~10
- Rows per day: `10 rows/s × 86,400 s ≈ 864,000 rows`
- Storage per day: `864,000 × 100 bytes ≈ 86,400,000 bytes ≈ 82 MB`

So you can think of it as roughly **80–130 MB per day**, depending on the exact number of parameters and JSON size.

### 6.5. How much data is that over time?

Very rough guide (with the default 2 s delay):

- **1 day**  → ~80–130 MB
- **1 week** → ~0.6–0.9 GB
- **1 month (30 days)** → ~2.5–4 GB
- **1 year** → ~30–50 GB

These numbers assume continuous 24/7 logging. If you increase `delay()` (slower sampling), all of these numbers scale down linearly.

### 6.6. When should I worry?

SQLite itself can handle database files in the tens of gigabytes, but practical considerations:

- Once `sensors.db` gets above **a few GB**, you may want to:
  - Back it up occasionally.
  - Consider a simple **retention policy** (for example, delete rows older than 90 days).
- If you reduce sampling (e.g. `delay(5000)` → one cycle every 5 s), storage per day drops proportionally.

### 6.7. Quick rule of thumb

If you are unsure, you can use this simple formula:

```text
rows_per_second ≈ (parameters_per_cycle) / (seconds_per_cycle)
rows_per_day    ≈ rows_per_second × 86,400
storage_per_day ≈ rows_per_day × 100 bytes
```

Then convert bytes → MB/GB as needed.

---

## 7. Minimal checklist (for you or collaborators)

1. Clone repo.
2. In firmware folder: create/edit `config.h` with Wi‑Fi and backend URL.
3. Flash ESP32 with the firmware.
4. In backend folder: install Python dependencies and run the FastAPI server.
5. Open the dashboard (Lovable or Python) and point it at the backend.

Once this is set up, all sensor data will be stored locally and visualised through the web dashboard on any machine that follows these steps.
