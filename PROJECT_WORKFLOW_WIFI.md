# Plant Monitoring Sensor Network – Wi‑Fi Workflow Summary

## 1. Project Basics
- **Project Name:** Plant Monitoring Sensor Network (Wi‑Fi MVP)
- **Client Name:** John Bambery
- **Scope:** Two battery‑powered, weatherproof sensor nodes + one local Wi‑Fi backend + web dashboard
- **Communication:** Wi‑Fi only (no LoRa in current phase)
- **Data Path (high level):** Sensors → ESP32 nodes → Wi‑Fi → Python FastAPI backend (SQLite) → React/Vite web dashboard

---

## 2. High‑Level System Architecture
- **Sensor Node (per node)**
  - ESP32‑S3 microcontroller
  - Environmental sensors (temperature, humidity, pressure, light, soil EC/pH, etc.)
  - Gas / VOC sensors (e.g. H2S, NH3, CO, O3, alcohol, formaldehyde)
  - RS485 / UART / I²C buses with muxes
  - Li‑ion battery + (optional) solar charging
  - Weatherproof enclosure + cable glands
- **Network & Backend**
  - Node connects to local Wi‑Fi (2.4 GHz)
  - HTTP POST JSON to backend: `http://PC_LAN_IP:8000/api/ingest`
  - Backend: FastAPI + SQLite (`sensors.db`)
  - API endpoints: `/api/ingest`, `/api/sensors/current`, `/api/sensors/history`, `/api/overview`, `/api/health`
- **Web Dashboard**
  - React/Vite/Lovable app on client PC
  - Uses `VITE_BACKEND_URL=http://PC_LAN_IP:8000`
  - Views: Overview, Sensors list, Sensor detail/history charts

---

## 3. End‑to‑End Workflow (Linear Steps)

1. **Node Power‑Up & Wi‑Fi Join**
   - ESP32 powers from battery
   - ESP32 reads Wi‑Fi SSID/password from `config.h`
   - ESP32 connects to project Wi‑Fi and obtains IP address

2. **Sensor Measurement Cycle (per loop)**
   - Select active I²C mux channels
   - Read environmental sensors (MS8607, BH1750, soil EC/pH, etc.)
   - Read gas sensors (MULTIGAS, CH4, alcohol, HCHO)
   - Update local health/diagnostic flags

3. **Build JSON Payload on ESP32**
   - Create root object: `ts_ms` (millis since boot)
   - Add nested objects for each sensor:
     - `MS8607` → `T`, `RH`, `P`
     - `BH1750` → `lux`
     - `ALCOHOL` → `ppm`
     - `CH4` → `lel_pct`, `tempC`, `err`
     - `HCHO_UART` → `ppm`
     - `SOIL_EC_PH` → `ec_mS_per_cm`, `pH` (when available)
     - `MULTIGAS` → `H2S`, `O2`, `NH3`, `CO`, `O3` blocks
   - Add `summary` block: total sensors, offline count, offline list

4. **Wi‑Fi HTTP POST to Backend**
   - If `WiFi.status() == WL_CONNECTED`:
     - `HTTPClient` POST to `BACKEND_URL`
     - Content‑Type: `application/json`
     - Log HTTP status and any error to serial

5. **Backend Ingest & Storage**
   - FastAPI `/api/ingest` receives JSON
   - Use server local time `ts = datetime.now()` as canonical timestamp
   - For each sensor block:
     - Map fields to logical parameters (e.g. `temperature_c`, `humidity_rh`, `co_ppm`, `o2_percent_vol`)
     - Insert rows into `measurements` table:
       - `ts`, `sensor_name`, `parameter`, `value`, `unit`
   - Result: time‑series data appended to `sensors.db`

6. **Backend Aggregation for Dashboard**
   - `/api/sensors/current`:
     - For each `(sensor_name, parameter)`, select latest `ts`
     - Group by `sensor_name` and package into `SensorStatus` objects
     - Mark `online` if `lastSeen >= now - 10 s`
   - `/api/sensors/history`:
     - Filter `measurements` by `sensor_name`, `parameter`, `ts >= now - hours`
     - Return ordered list for charts
   - `/api/overview`:
     - Count total sensors, online, offline
     - Count total measurement rows
     - Compute 24h averages for `temperature_c` and `humidity_rh` from MS8607

7. **Web Dashboard Display**
   - Frontend polls backend every few seconds:
     - `/api/sensors/current` for cards
     - `/api/overview` for top stats
   - Sensor cards:
     - Show sensor name, category, online badge
     - Display latest values for main parameters
     - If offline, show "Last seen X minutes ago" (from `lastSeen`)
   - Sensor detail page:
     - Summary of current readings
     - Time‑range selector (24h, 7d, 30d, All)
     - Charts from `/api/sensors/history`
     - Statistics summary (min / max / avg from history)

8. **User Monitoring & Interaction**
   - User opens dashboard URL (e.g. `http://localhost:5173`)
   - User filters sensors by category (Environment / Light / Gas / Soil)
   - User drills into specific sensors for historical analysis

---

## 4. Firmware / Hardware Task Breakdown (Wi‑Fi Only)

- **Hardware Selection & Assembly**
  - Choose ESP32‑S3 devkit and all required sensors
  - Design wiring: I²C buses, mux channels, UART, RS485, power rails
  - Assemble prototype on breadboard
  - Move to PCB + enclosure once stable

- **Firmware Core (ESP32)**
  - Wi‑Fi connect logic using `config.h`
  - I²C mux helper functions and sensor initialisation
  - Per‑cycle sensor read functions with offline detection
  - JSON build using ArduinoJson (including `MULTIGAS` block)
  - HTTP POST implementation and basic retry/error logging
  - Simple diagnostics over serial (sensor summary, HTTP status)

- **Power & Enclosure**
  - Select Li‑ion battery and protection circuitry
  - Integrate optional solar charger
  - Design IP‑rated enclosure and cable routing
  - Add LED indicators for status (power, Wi‑Fi, error)

---

## 5. Backend & Dashboard Task Breakdown

- **Backend (FastAPI + SQLite)**
  - Define Pydantic models for ingest and API responses
  - Implement `/api/ingest` mapping from firmware JSON → DB rows
  - Implement `/api/sensors/current`, `/api/sensors/history`, `/api/overview`, `/api/health`
  - Add CORS rules for local Vite dev server
  - Add simple logging and debug prints (ingest keys, MULTIGAS keys)

- **Database (SQLite)**
  - Create `measurements` table
  - Optionally add indexes on `(sensor_name, parameter, ts)`
  - Plan retention policy if DB grows beyond a few GB (e.g. delete >90 days)

- **Web Dashboard (React/Vite)**
  - `.env` with `VITE_BACKEND_URL`
  - `useSensorData` + `useSensorHistory` hooks to call backend
  - Sensor list, filters, and cards
  - Sensor detail page with charts and stats
  - Overview page with total sensors, online/offline, total measurements, averages

---

## 6. Notes for Future Extensions (NOT in current Wi‑Fi MVP)

- Add LoRa branch for long‑range communication (ESP32 + LoRa transceivers)
- Add central LoRa "gateway" node that forwards data to the same FastAPI backend
- Add cloud IoT integration (MQTT/HTTPS) as an alternate data path
- Add remote configuration (sampling interval, thresholds) via backend APIs
