# OOPS CP — Smart Home Control System

A C++-based Smart Home Control System developed as an Object-Oriented Programming (OOP) course project.

The project simulates a smart home environment where different household appliances can be registered, controlled, configured, and monitored through a modular software architecture.

> **Current Status:** Core device-management architecture implemented. Advanced automation, scheduling, energy optimization, and intelligent control modules are planned for future development.

---

## 🎯 Project Objective

The objective of this project is to design a modular and extensible Smart Home Control System using **C++ and Object-Oriented Programming principles**.

The system is designed to provide a foundation for:

* Managing multiple smart appliances
* Monitoring appliance states
* Controlling devices programmatically
* Supporting device-specific configurations
* Managing devices through a centralized controller
* Extending the system with automation and intelligent decision-making in future versions

---

## 🏠 Current Features

### Device Management

The current system supports virtual smart-home appliances such as:

* 💡 Light
* 🌀 Fan
* ❄️ Air Conditioner
* 🔥 Heater

Each device contains information such as:

* Device ID
* Device name
* Room
* Power consumption
* Current ON/OFF state
* Runtime
* Device-specific settings

### Device Control

The system currently supports:

* Turning devices ON
* Turning devices OFF
* Finding devices using unique IDs
* Displaying the current state of all devices

### Device-Specific Configuration

Different appliances support their own configurations.

**Fan**

* Speed control from 1 to 5

**Air Conditioner**

* Temperature control from 16°C to 30°C

**Heater**

* Temperature control from 20°C to 45°C

### Exception Handling

Invalid device operations and invalid settings are handled using C++ exception handling mechanisms.

Examples include:

* Invalid fan speed
* Invalid AC temperature
* Invalid heater temperature
* Device not found
* Null device registration

---

## 🧠 OOP Concepts Used

This project demonstrates several important Object-Oriented Programming concepts.

### 1. Encapsulation

Device properties and internal state are maintained within classes, with controlled access through member functions.

### 2. Inheritance

Specific appliances inherit common functionality from the base `Appliance` class.

```text
                 Appliance
                /    |    \
             Light  Fan   AC
                         \
                         Heater
```

### 3. Polymorphism

Virtual functions such as `showStatus()` allow different appliance types to provide their own implementations.

### 4. Abstraction

Common appliance functionality is represented through the `Appliance` abstraction, while individual devices implement their specific behavior.

### 5. Constructors and Destructors

Constructors initialize appliance objects, while destructors are used for resource cleanup.

### 6. Exception Handling

The system uses C++ exceptions to handle invalid operations and maintain safe execution.

### 7. Dynamic Memory Allocation

The current implementation creates appliance objects dynamically and manages their lifetime through `DeviceManager`.

---

## 🏗️ Project Architecture

The project follows a modular architecture instead of placing all functionality inside a single `main.cpp`.

```text
OOPS CP/
│
├── main.cpp
│
├── core/
│   │
│   ├── devices/
│   │   ├── Appliance.h
│   │   ├── Appliance.cpp
│   │   ├── Light.h
│   │   ├── Light.cpp
│   │   ├── Fan.h
│   │   ├── Fan.cpp
│   │   ├── AC.h
│   │   ├── AC.cpp
│   │   ├── Heater.h
│   │   └── Heater.cpp
│   │
│   ├── state/
│   │   ├── DeviceState.h
│   │   └── HomeState.h
│   │
│   └── control/
│       ├── DeviceManager.h
│       └── DeviceManager.cpp
│
└── README.md
```

---

## 🔄 How the System Works

The basic flow of the current system is:

```text
Application Start
       ↓
DeviceManager Created
       ↓
Virtual Devices Registered
       ↓
Device ID Used for Identification
       ↓
Device Control / Configuration
       ↓
Device State Updated
       ↓
Current Home State Displayed
```

For example:

```text
LIVING_FAN
     ↓
Turn ON
     ↓
Set Speed = 3
     ↓
Update Device State
     ↓
Display Current Status
```

---

## 💻 Technologies Used

* **C++**
* **Object-Oriented Programming**
* **STL**
* **MSYS2 / MinGW-w64 GCC**
* **Visual Studio Code**
* **Git & GitHub**

The current implementation is designed as a C++ console application.

---

## 🚀 Planned Development

The current implementation is the foundation of a larger smart-home simulation system.

Future modules may include:

### Phase 1 — Core System

* [x] Device abstraction
* [x] Multiple appliance types
* [x] Device registration
* [x] Device control
* [x] Device state representation
* [x] Exception handling

### Phase 2 — Smart Home Features

* [ ] Room and zone management
* [ ] Device scheduling
* [ ] Automation rules
* [ ] Scenes / modes
* [ ] Event management
* [ ] Persistent device data

### Phase 3 — Energy Management

* [ ] Energy consumption tracking
* [ ] Electricity tariff calculation
* [ ] Energy usage reports
* [ ] Energy budgets
* [ ] Peak-load monitoring

### Phase 4 — Intelligent Control

* [ ] State-aware decision making
* [ ] Control intervention evaluation
* [ ] Comfort-aware optimization
* [ ] User override tracking
* [ ] Historical behavior analysis
* [ ] Energy vs. comfort trade-off analysis

### Phase 5 — Software Platform

* [ ] Backend/API integration
* [ ] Database integration
* [ ] Web-based dashboard
* [ ] Device simulation engine
* [ ] Authentication and user management
* [ ] Monitoring and analytics

### Optional Future Extension

* [ ] ESP32 integration
* [ ] MQTT communication
* [ ] Physical sensor integration
* [ ] Real-time IoT device control

---

## 🔬 Research Direction

Beyond basic appliance automation, the project is intended to explore **state-aware residential device control**.

Instead of simply asking:

> "Which device should be turned ON or OFF?"

the future system aims to evaluate:

> "Should this control intervention happen at all, considering energy benefit, device transition cost, comfort, possible user reversal, and future consequences?"

A future decision model may consider factors such as:

```text
Energy Benefit
       -
Transition Cost
       -
Comfort Penalty
       -
Disruption Risk
       -
Future Cost
       +
Historical Behaviour
       ↓
Intervention Decision
```

This direction allows the project to evolve from a basic smart-home controller into an experimental platform for intelligent residential control.

---

## 📊 Evaluation Metrics

Future intelligent-control modules can be evaluated using:

* Total energy consumption
* Energy saved
* Peak power demand
* Number of control interventions
* Number of user overrides
* Number of reversed interventions
* Comfort violations
* Control stability
* Estimated electricity cost

The proposed system can eventually be compared against:

1. Manual Control
2. Rule-Based Automation
3. Energy-Priority Control
4. Proposed State-Aware Control

---

## ▶️ Building the Project

Make sure GCC is installed and available in your terminal.

Check:

```bash
g++ --version
```

Compile the project:

```bash
g++ -std=c++17 main.cpp core/devices/Appliance.cpp core/devices/Light.cpp core/devices/Fan.cpp core/devices/AC.cpp core/devices/Heater.cpp core/control/DeviceManager.cpp -o SmartHome.exe
```

Run:

```bash
./SmartHome.exe
```

### Windows PowerShell

```powershell
.\SmartHome.exe
```

> **Note:** On some managed Windows systems, application-control policies such as Device Guard may prevent locally compiled executables from running even when compilation succeeds.

---

## 📁 Repository Structure

The repository is intentionally separated into modules so that new functionality can be added without rewriting the entire application.

```text
core/
├── devices/      → Appliance definitions and implementations
├── state/        → Device and home state representation
└── control/      → Device management and control logic
```

---

## 🎓 Academic Focus

This project demonstrates practical application of:

* Classes and Objects
* Constructors and Destructors
* Inheritance
* Polymorphism
* Encapsulation
* Abstraction
* Virtual Functions
* Dynamic Memory Allocation
* Exception Handling
* Header and Implementation Files
* Modular Software Design
* STL Containers

---

## 👩‍💻 Project

**OOPS CP**

Developed as part of an Object-Oriented Programming course project.

---

## 📌 Project Philosophy

The project starts with a simple virtual smart-home controller and is designed to evolve incrementally into a modular software platform.

The focus is not only on controlling devices, but on exploring how software can make **better, safer, and more context-aware decisions** about residential device control.

## Current SmartHomeOS application

The repository also contains the React/Vite dashboard and the C++17 HTTP API.
The API opens the root-level `smarthome.db` relative to its working directory.
Run it from the repository root; do not replace that database with a test or
demo database.

### Build and run on Windows

Build the API with the installed MSYS2 UCRT64 GCC toolchain. The development
executable name below deliberately avoids replacing the existing
`SmartHomeAPI.exe`:

```powershell
g++ -std=c++17 -pthread api_main.cpp `
  core\devices\Appliance.cpp core\devices\Light.cpp core\devices\Fan.cpp `
  core\devices\AC.cpp core\devices\Heater.cpp `
  core\devices\GenericAppliance.cpp core\control\DeviceManager.cpp `
  core\home\Room.cpp core\home\SmartHome.cpp `
  core\Storage\Database.cpp core\Storage\EnergyTracker.cpp `
  -I. -Ithird_party -lsqlite3 -lws2_32 -lwinhttp -o SmartHomeAPI-dev.exe
.\SmartHomeAPI-dev.exe
```

The API listens on `127.0.0.1:8080` by default. `SMARTHOME_API_PORT` can
select a different local port for isolated development and tests. Start the
frontend in a separate terminal:

```powershell
npm --prefix frontend run dev -- --host localhost --port 5173
```

Run the backend from the repository root so it opens the existing root
`smarthome.db`. If an older API process already owns port 8080, stop that
process deliberately before starting the updated development executable; do
not replace the database or overwrite `SmartHomeAPI.exe`.
If an older server returns `404` for `/api/automation/room-settings`, it is
still running an executable built before that route was added: rebuild the API
and restart that process. The frontend uses the same generic API for every
room; it does not have room-specific endpoints.

### Existing API and preferences

The API exposes room and device management, power control, estimated energy
sessions, and persistent profiles/preferences:

* `GET /api/profiles` and `POST /api/profiles`
* `GET /api/profiles/{id}/preferences?roomName={room}`
* `PUT /api/profiles/{id}/preferences`
* `POST /api/profiles/{id}/preferences/apply` with `{"roomName":"Bedroom"}`
* `GET /api/energy/summary`
* `POST /api/assistant/chat` with `{"question":"..."}` (maximum 1000 characters)
* `GET /api/devices/{id}` for a device and its supported capabilities
* `PATCH /api/devices/{id}` for `name` and/or `powerWatts`
* `PATCH /api/devices/{id}/settings` for one supported setting at a time
* Existing `POST /api/devices`, `GET /api/devices`,
  `POST /api/devices/{id}/power`, and `DELETE /api/devices/{id}` routes

Preference values of `-1` mean unspecified. Supported ranges are AC
temperature 16–30 °C, heater temperature 20–45 °C, and fan speed 1–5.
Selecting a profile or saving preferences does not change appliance state.
The dashboard shows an apply preview and asks for confirmation before the
backend applies saved preferences. Unsupported appliances are reported in
the per-device result.

### Device settings and persistence

Devices are independently identified by their unique IDs; multiple devices
with the same type can occupy one room. The Add Device form supports an initial
power state, target AC/fan/heater setting, and the capability profile for a
light. Existing generic appliance types remain basic ON/OFF devices.

* AC target temperature: **16–30 °C**; operating modes, eco, swing, and AC fan
  speed are not implemented by the current AC class.
* Fan speed: **1–5**; it can be adjusted while the fan is OFF.
* Heater target temperature: **20–45 °C**. This is a target setting, not a
  measured room temperature.
* Light capability profiles: `standard`, `warm-white`, `cool-white` (no
  adjustable controls), `dimmable` (brightness), `rgb` (brightness and RGB
  color), `tunable-white` (brightness and 2700–6500 K color temperature), and
  `smart-bulb` (brightness, RGB color, and 2700–6500 K color temperature).
  These are simulated capabilities stored by SmartHomeOS; no physical bulb
  integration is claimed. Brightness is 0–100%; RGB colors must be `#RRGGBB`.
  Stored light configuration is independent from its ON/OFF state.

Setting changes are validated in the C++ device classes and persisted before
the API reports success. `PATCH /api/devices/{id}/settings` accepts exactly
one setting property per request: `setting` for AC/fan/heater, or `brightness`,
`color`, and `colorTemperature` for lights that support them. Device metadata
edits currently cover the display name and rated watts. If rated watts change
while a device is ON, the active energy session is closed and a new one starts
with the updated rated value; this remains an estimate, not live metering.

On startup, the backend applies additive SQLite migration version 1 if needed.
It adds `light_type`, `brightness`, `color`, and `color_temperature` columns
with defaults that preserve all existing device rows and settings. The
migration is transactional and does not rebuild or seed the database. Before
the first migration, it creates a SQLite online-backup copy alongside the
database using the `smarthome.db.pre-v1-<timestamp>-<number>.bak` naming
pattern. It will not overwrite an existing backup; startup stops if backup
creation fails.

Energy values are estimates calculated from rated watts and recorded ON
duration; they are not sensor measurements. The bill estimator uses editable,
demonstration tariff assumptions held in frontend state.

The energy monitor reports trailing 24-hour, 7-day, and 30-day estimates,
previous-window comparisons, and room/appliance breakdowns. The estimates
overlap recorded ON-session intervals with each reporting window and do not
come from a physical meter.

### Energy assistant configuration

The assistant always works without credentials by returning a deterministic
summary and an advisory review suggestion based on backend-calculated energy
data. Optional cloud explanations are sent only by the C++ backend using
Windows WinHTTP. Configure an HTTPS endpoint that accepts the OpenAI-compatible
Chat Completions request/response shape and set these process environment
variables before starting the backend:

* `AI_API_KEY` — provider key; never add a real value to source control.
* `AI_API_URL` — full HTTPS chat-completions endpoint.
* `AI_MODEL` — provider model identifier.

See [.env.example](./.env.example) for placeholders. The application does not
read `.env` files automatically. The integration was tested in no-key fallback
mode; a real provider request requires the user's provider configuration and
has not been tested here.

### Room-scoped manual control and camera automation

Every selected room offers **Manual Control** and **Camera Automation**.
Manual device controls remain usable without the Python service or a camera.
Camera recognition is opt-in: select a room and enable that room's camera
monitoring and the global automation setting. Once explicitly enabled, opening
that room starts monitoring and displays a camera-active indicator; the user
can stop it immediately or switch to manual controls. Rooms remain disabled by
default. Room changes replace the active session; recognition stopping never
powers devices off.
Unknown faces must persist across three single-face observations before they
are reported as unknown; low-light, low-contrast, and blurry observations are
treated as uncertain and never resolve household preferences. These image
quality screens are conservative heuristics and still need validation with the
household's actual camera and lighting. Recognized preferences are previewed
and require an explicit confirmation the first time. The profile's
room-preference form has a separate
automatic-application option. If enabled, subsequent consistent matches in
that same room can apply the saved preferences automatically after the first
successful confirmed application. Changing the saved preferences or the
automatic-application option requires first confirmation again.

For Windows, install the local face-service dependency and run the service from
the repository root in a separate terminal:

```powershell
python -m pip install -r requirements-face.txt
python face_automation_service.py
```

The service listens only on `127.0.0.1:8765`. It requires Windows because
enrollment templates are protected using the current account's DPAPI and stored
under `%LOCALAPPDATA%\SmartHomeOS\face_templates.dpapi`. `SMARTHOME_FACE_PORT`
and `SMARTHOME_FACE_MATCH_THRESHOLD` can override its port and LBPH distance
threshold; a lower distance is a closer match. The default threshold is 55,
and three consistent observations are required. Grant camera permission to
the local Vite page when prompted. Enrollment is available on the Facial
Recognition Setup page and requires selecting an existing profile and checking
the explicit consent box. Delete enrollment there to remove its local
protected template and metadata.
The frontend polls the actual local `/health` endpoint; if the Python process
is not listening, the service is offline rather than assumed healthy. The
service does not open a webcam itself: the browser owns the camera stream and
sends frames locally. A single service session can be active at once, so one
shared webcam cannot monitor multiple rooms simultaneously. The C++ and Python
session records both bind the active session to its room.

Raw camera frames are processed locally and posted only to the loopback face
service. The C++ API persists enrollment-to-profile metadata, the global
enable setting, and automation events, but not frames or biometric templates.
The relevant API routes are:

| Method | Route | Purpose |
| --- | --- | --- |
| `GET` | `/api/automation/status` | Read global enablement and active room session |
| `PUT` | `/api/automation/settings` | Persist `{"enabled":true}` or `{"enabled":false}` |
| `GET` | `/api/automation/room-settings` | List camera settings for every current room; unconfigured rooms default to disabled |
| `GET` | `/api/automation/room-settings?roomName={name}` | Read `cameraEnabled` for one room |
| `PUT` | `/api/automation/room-settings` | Persist `{"roomName":"Bedroom","cameraEnabled":true}` |
| `GET`, `POST` | `/api/automation/enrollments` | List or register an existing profile with consent |
| `DELETE` | `/api/automation/enrollments/{id}` | Delete enrollment metadata |
| `GET` | `/api/automation/events?limit=5` | Read persisted event summaries |
| `POST` | `/api/automation/sessions` | Start a room-bound session with `{"roomName":"Bedroom"}` |
| `GET`, `DELETE` | `/api/automation/sessions/{sessionId}` | Read or stop a session |
| `POST` | `/api/automation/sessions/{sessionId}/observation` | Send a local recognition observation |
| `POST` | `/api/automation/sessions/{sessionId}/resolve` | Resolve the accepted enrollment to its profile preferences |
| `POST` | `/api/profiles/{id}/preferences/apply` | Apply the previewed preferences through the existing C++ device path |

The Python face service exposes `GET /health`; `POST /recognition/sessions`
starts a room-bound session; `GET /recognition/sessions/{sessionId}` reports
its room; and `DELETE /recognition/sessions/{sessionId}` stops it. Settings
are derived from the database's room records, so rooms added later receive the
same disabled-by-default behavior.

The confirmation request includes `roomName`, `automationSessionId`, and
`confirmed: true`. The C++ API rechecks the active room, fresh matched identity,
linked profile, saved preference values, and device capabilities before
applying. A profile can be applied only once per identity/session; an audit
event records the room, profile, time, outcome, and changed settings. Unknown,
pending, or ambiguous recognition never resolves preferences. Multiple faces
are reported as ambiguous and no identity is selected.

On startup, the additive SQLite version 2 migration creates the automation
tables without rebuilding or seeding existing data. Migration version 4 adds
per-room camera opt-in and per-preference automatic-application confirmation
fields; existing records default to disabled/unconfirmed. Before each
migration, the backend creates an online backup named
`smarthome.db.pre-v<version>-<timestamp>-<number>.bak`; it stops if the backup
fails.
The local service supports OpenCV Haar detection and LBPH matching, not secure
authentication or liveness detection. Real camera capture and matching
accuracy depend on the user's local camera, lighting, and enrollment; neither
is guaranteed by these tests. Device changes operate the SmartHomeOS simulator,
not physical appliances.

### Season and mood modes

The **Season & Mood Modes** page provides four manually selected suggested
season presets (Summer, Monsoon, Winter, Spring) and six mood overlays (Focus /
Study, Relax, Romantic, Movie Night, Sleep, Energize). These are configurable
suggestions, not weather detection or medical/environmental advice. Seasonal
defaults vary by room category where appropriate; unsupported properties are
shown as skipped. Mood values override a seasonal value only for the same
device and property. User-created overrides target one existing device and
property and are validated against that device's real simulated capability.
For example, standard non-dimmable lights do not accept brightness, color, or
color-temperature settings. AC operating modes are not implemented.

Select one room, then choose a season and/or mood. The preview includes every
registered device in that room, including explicit ON, OFF, CONFIGURE_ONLY,
and KEEP_CURRENT policies, supported property changes, and skipped settings.
The user confirms with **Apply Mode**. A preview token expires after five
minutes and is single-use; applying rejects it if relevant device values or
preset overrides changed since preview. Manual controls remain available and
saved profile preferences are never modified. Each preset supports a
device-ID-specific policy override and property overrides; the backend checks
device existence, capabilities, and ranges. Unsupported settings are reported
as skipped rather than being sent through an incompatible control.

Suggested default policies are:

* **Summer:** ACs and fans ON with room-aware temperature/speed settings;
  heaters OFF; lights KEEP_CURRENT.
* **Monsoon:** fans ON with room-aware speeds; ACs CONFIGURE_ONLY with a
  room-aware setpoint (their power state is preserved); heaters and lights
  KEEP_CURRENT. No humidity or rain is measured.
* **Winter:** heaters ON with room-aware temperatures; ACs OFF; fans
  CONFIGURE_ONLY at a low/moderate speed; lights KEEP_CURRENT.
* **Spring:** ACs and heaters OFF; fans CONFIGURE_ONLY at a moderate speed;
  lights KEEP_CURRENT.
* **Focus / Study:** study-room lights ON with supported bright/neutral
  settings and fan ON with the configured speed.
* **Relax:** lights and bedroom/living fans ON with supported softer settings.
* **Romantic:** ambient-capable lights ON with supported warm/color settings;
  unsupported standard lights are left unchanged.
* **Movie Night:** dimmable lights ON at low brightness; other devices stay
  unchanged.
* **Sleep:** bedroom lights OFF; bedroom fan ON at a low setting; other devices
  stay unchanged.
* **Energize:** study/living lights and fans ON with supported bright and
  fan-speed settings.

When both are selected, a mood's explicit device policy overlays the seasonal
policy for that device; mood settings overlay only matching device properties.
Policy overrides use
`{"deviceId":"LIVING_RGB","property":"policy","value":"CONFIGURE_ONLY"}`.
Supported settings include `setting` (AC 16–30 °C, fan 1–5, heater 20–45 °C),
`brightness` (0–100), `color` (`#RRGGBB`), and `colorTemperature`
(2700–6500 K). The legacy `power` boolean property remains accepted as an
ON/OFF override.

Successful or partial applications record per-property outcomes and a snapshot
of every targeted device's supported state/settings immediately before the
changes. **Restore previous state** restores the latest unapplied snapshot for the selected room through
the C++ device control and persistence paths; it does not assume a universal
default. Failed restores remain retryable. The UI refreshes device state after
the backend reports the per-device results. Reapplying a mode with no changes
records the first no-change outcome for that combination and scope, but does
not create duplicate consecutive no-change history entries. Energy effects
are not calculated: the existing monitor estimates consumption from rated
watts and ON duration, not from adjustable temperatures, fan speeds, or
lighting settings.

Mode API routes:

| Method | Route | Purpose |
| --- | --- | --- |
| `GET` | `/api/modes/seasons` | List seasonal presets and saved device overrides |
| `GET` | `/api/modes/moods` | List mood presets and saved device overrides |
| `PUT` | `/api/modes/presets/{type}/{id}` | Replace a preset's device-specific `overrides` (`type` is `season` or `mood`) |
| `POST` | `/api/modes/preview` | Preview `{ "seasonId":"summer", "moodId":"relax", "roomNames":["Bedroom"], "deviceIds":[] }`; use an empty ID for an unselected mode |
| `POST` | `/api/modes/apply` | Apply a returned `previewId` with `{"previewId":"…","confirmed":true}` |
| `POST` | `/api/modes/restore` | Restore the latest snapshot; provide `{"confirmed":true,"roomName":"Bedroom"}` for room scope |
| `GET` | `/api/modes/history?limit=50` | Read persisted application and restoration outcomes |

A property customization override has the shape
`{"deviceId":"LIVING_RGB","property":"brightness","value":35}`. Supported
properties also include `policy` (`ON`, `OFF`, `KEEP_CURRENT`, or
`CONFIGURE_ONLY`). `power` (boolean), `setting` (AC 16–30 °C, fan 1–5, heater
20–45 °C), `brightness` (0–100), `color` (`#RRGGBB`), and `colorTemperature`
(2700–6500 K), limited to capabilities reported by the device API. The
backend validates every override and every proposed change.

The additive SQLite version 3 migration creates `mode_presets` and
`mode_history`; it does not rebuild or seed existing records. Before migration
the backend creates an online backup named
`smarthome.db.pre-v3-<timestamp>-<number>.bak` and stops if backup creation
fails. Start the API from the repository root as documented above.

### Verification

Run the isolated API integration suite with a separately built executable:

```powershell
python test_profile_preferences_api.py .\SmartHomeAPI-dev.exe
python test_modes_api.py .\SmartHomeAPI-dev.exe
```

The test makes an SQLite backup of `smarthome.db`, starts the API against that
temporary copy on an available local port, and discards the copy afterward.
It does not require or overwrite the active database. The API suite checks
independent camera settings, persistence after restart, room-bound sequential
sessions, and one newly created room; the session test uses the local face
service but does not open a physical camera. Frontend checks are:

```powershell
npm --prefix frontend run lint
npm --prefix frontend run build
```

The profiles/preferences workflow, confirmed apply action, estimated energy
analytics, assistant fallback, and local face-service unit/API behavior are
covered by repository tests. A real camera session, real household face-match
accuracy, liveness detection, and physical device operation have not been
verified. The configured cloud-provider path is not verified against a live
provider. Energy data must not be presented as measured usage.
