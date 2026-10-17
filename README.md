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

### Camera privacy and limitations

The Camera & Occupancy page uses browser `getUserMedia` only after Start camera
is pressed. The local preview is not recorded, stored, or sent to the backend
or cloud. Stopping the camera or navigating away releases the stream. The page
does not include an occupancy detector, so occupancy always remains Unknown;
the camera device was detected by Windows, but browser permission and live
capture were not exercised during verification.

### Verification

Run the isolated API integration suite with a separately built executable:

```powershell
python test_profile_preferences_api.py .\SmartHomeAPI-dev.exe
```

The test makes an SQLite backup of `smarthome.db`, starts the API against that
temporary copy on an available local port, and discards the copy afterward.
It does not require or overwrite the active database. Frontend checks are:

```powershell
npm --prefix frontend run lint
npm --prefix frontend run build
```

The profiles/preferences workflow, confirmed apply action, estimated energy
analytics, assistant fallback, and privacy-first local camera preview are
implemented. The configured cloud-provider path is not verified against a live
provider, occupancy inference is not implemented, and recommendations do not
yet combine profiles with occupancy. Energy data must not be presented as
measured usage.
