
import "./App.css";

import React, {
  useCallback,
  useEffect,
  useMemo,
  useState,
} from "react";

const API_URL = "http://localhost:8080/api";

const DEFAULT_TARIFF = {
  slab1: 3.96,
  slab2: 10.8,
  slab3: 15.03,
  slab4: 17.53,
  wheeling: 1.6,
  fixedCharge: 100,
  dutyPercent: 16,
};

function formatDuration(seconds) {
  const safeSeconds = Math.max(0, Math.floor(seconds || 0));
  const hours = Math.floor(safeSeconds / 3600);
  const minutes = Math.floor((safeSeconds % 3600) / 60);
  const remainingSeconds = safeSeconds % 60;

  if (hours > 0) return `${hours}h ${minutes}m`;
  if (minutes > 0) return `${minutes}m ${remainingSeconds}s`;
  return `${remainingSeconds}s`;
}

function formatDate(timestamp) {
  if (!timestamp || timestamp <= 0) return "—";

  return new Date(timestamp * 1000).toLocaleString("en-IN", {
    dateStyle: "short",
    timeStyle: "short",
  });
}

function calculateBill(units, tariff) {
  const first = Math.min(units, 100);
  const second = Math.min(Math.max(units - 100, 0), 200);
  const third = Math.min(Math.max(units - 300, 0), 200);
  const fourth = Math.max(units - 500, 0);

  const energyCharge =
    first * tariff.slab1 +
    second * tariff.slab2 +
    third * tariff.slab3 +
    fourth * tariff.slab4;

  const wheelingCharge = units * tariff.wheeling;
  const fixedCharge = tariff.fixedCharge;
  const duty = (energyCharge + wheelingCharge) * (tariff.dutyPercent / 100);

  return {
    units,
    first,
    second,
    third,
    fourth,
    energyCharge,
    wheelingCharge,
    fixedCharge,
    duty,
    total: energyCharge + wheelingCharge + fixedCharge + duty,
  };
}

function App() {
  const [rooms, setRooms] = useState([]);
  const [roomNameInput, setRoomNameInput] = useState("");
  const [editingRoom, setEditingRoom] = useState(null);
  const [editingRoomName, setEditingRoomName] = useState("");
  const [roomBusy, setRoomBusy] = useState(false);
  const [roomMessage, setRoomMessage] = useState("");
  const [devices, setDevices] = useState([]);
  const [sessions, setSessions] = useState([]);

  const [loading, setLoading] = useState(true);
  const [energyLoading, setEnergyLoading] = useState(true);
  const [error, setError] = useState("");
  const [energyError, setEnergyError] = useState("");

  const [search, setSearch] = useState("");
  const [selectedRoom, setSelectedRoom] = useState("All Rooms");
  const [updatingDevices, setUpdatingDevices] = useState({});
  const [bulkUpdating, setBulkUpdating] = useState(false);

  const [showDeviceForm, setShowDeviceForm] = useState(false);
  const [deviceBusy, setDeviceBusy] = useState(false);
  const [deviceMessage, setDeviceMessage] = useState("");
  const [deletingDevices, setDeletingDevices] = useState({});

  const [deviceForm, setDeviceForm] = useState({
    id: "",
    name: "",
    type: "Light",
    roomName: "",
    powerWatts: "60",
    temporary: true,
    lifetimeValue: "30",
    lifetimeUnit: "minutes",
  });

  const [tariff, setTariff] = useState(DEFAULT_TARIFF);

  const [tariffInputs, setTariffInputs] = useState({
    ...DEFAULT_TARIFF,
  });

  const [showTariffSettings, setShowTariffSettings] = useState(false);
  const [activeSection, setActiveSection] = useState("overview");

  const fetchJson = async (url, options = {}) => {
    const response = await fetch(url, options);

    if (!response.ok) {
      throw new Error(`Request failed (${response.status})`);
    }

    return response.json();
  };


  const loadProfiles = useCallback(async () => {
    try {
      const data = await fetchJson(`${API_URL}/profiles`);
      const list = Array.isArray(data) ? data : [];

      setProfiles(list);
      setProfileMessage("");
    } catch (err) {
      setProfileMessage(`Could not load profiles: ${err.message}`);
    }
  }, []);

  const createProfile = async (event) => {
    event.preventDefault();

    const displayName = profileNameInput.trim();

    if (!displayName) {
      setProfileMessage("Enter a profile name.");
      return;
    }

    setProfileBusy(true);
    setProfileMessage("");

    try {
      const id = `user_${Date.now()}`;

      await fetchJson(`${API_URL}/profiles`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ id, displayName }),
      });

      setProfileNameInput("");
      await loadProfiles();
      setSelectedProfileId(id);
      setProfileMessage(`Profile "${displayName}" created successfully.`);
    } catch (err) {
      setProfileMessage(`Could not create profile: ${err.message}`);
    } finally {
      setProfileBusy(false);
    }
  };

  const loadDashboard = useCallback(async (showLoading = false) => {
    if (showLoading) setLoading(true);

    try {
      const [roomsData, devicesData] = await Promise.all([
        fetchJson(`${API_URL}/rooms`),
        fetchJson(`${API_URL}/devices`),
      ]);

      setRooms(Array.isArray(roomsData) ? roomsData : []);
      setDevices(Array.isArray(devicesData) ? devicesData : []);
      setError("");
    } catch (err) {
      setError(
        `Unable to load SmartHomeOS data. Check that SmartHomeAPI.exe is running. ${err.message}`
      );
    } finally {
      setLoading(false);
    }
  }, []);


  const [profiles, setProfiles] = useState([]);
  const [selectedProfileId, setSelectedProfileId] = useState("");
  const [profileNameInput, setProfileNameInput] = useState("");
  const [profileBusy, setProfileBusy] = useState(false);
  const [profileMessage, setProfileMessage] = useState("");

  const [preferenceRoom, setPreferenceRoom] = useState("");
  const [preferences, setPreferences] = useState({
    acTemperature: -1,
    heaterTemperature: -1,
    fanSpeed: -1,
    lightOn: -1,
  });


  const getApiError = async (response) => {
    try {
      const data = await response.json();
      return data.error || data.message || `Request failed (${response.status})`;
    } catch {
      return `Request failed (${response.status})`;
    }
  };

  const manageRoom = async (endpoint, payload, successMessage) => {
    setRoomBusy(true);
    setRoomMessage("");
    setError("");

    try {
      const response = await fetch(`${API_URL}/rooms${endpoint}`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload),
      });

      if (!response.ok) {
        throw new Error(await getApiError(response));
      }

      setRoomMessage(successMessage);
      await loadDashboard(false);
      return true;
    } catch (err) {
      setRoomMessage(`Error: ${err.message}`);
      return false;
    } finally {
      setRoomBusy(false);
    }
  };

  const createRoom = async (event) => {
    event.preventDefault();

    const name = roomNameInput.trim();
    if (!name) {
      setRoomMessage("Enter a room name.");
      return;
    }

    const success = await manageRoom(
      "",
      { name },
      `Room "${name}" created successfully.`
    );

    if (success) {
      setRoomNameInput("");
    }
  };

  const renameRoom = async (event) => {
    event.preventDefault();

    if (!editingRoom) return;

    const oldName = editingRoom;
    const newName = editingRoomName.trim();

    if (!newName) {
      setRoomMessage("Enter a new room name.");
      return;
    }

    const success = await manageRoom(
      "/rename",
      { oldName, newName },
      `Room renamed to "${newName}".`
    );

    if (success) {
      setEditingRoom(null);
      setEditingRoomName("");

      setSelectedRoom((current) =>
        current === oldName ? newName : current
      );
    }
  };

  const deleteRoom = async (name) => {
    const confirmed = window.confirm(
      `Delete "${name}"? Only empty rooms can be deleted.`
    );

    if (!confirmed) return;

    const success = await manageRoom(
      "/delete",
      { name },
      `Room "${name}" deleted successfully.`
    );

    if (success && selectedRoom === name) {
      setSelectedRoom("All Rooms");
    }
  };

  const loadEnergySessions = useCallback(async () => {
    try {
      const data = await fetchJson(`${API_URL}/energy/sessions`);

      const sessionList = Array.isArray(data)
        ? data
        : Array.isArray(data.value)
          ? data.value
          : [];

      setSessions(sessionList);
      setEnergyError("");
    } catch (err) {
      setEnergyError(
        `Unable to load energy history. ${err.message}`
      );
    } finally {
      setEnergyLoading(false);
    }
  }, []);

  useEffect(() => {
    loadDashboard(true);
    loadEnergySessions();
    loadProfiles();

    const interval = setInterval(() => {
      loadDashboard(false);
      loadEnergySessions();
    }, 10000);

    return () => clearInterval(interval);
  }, [loadDashboard, loadEnergySessions, loadProfiles]);

  const isDeviceOn = (device) => {
    const state = device.state ?? device.status ?? device.isOn;

    if (typeof state === "boolean") return state;

    return String(state).toUpperCase() === "ON";
  };

  const getDeviceId = (device) =>
    device.id ?? device.deviceId ?? device.name;

  const getDeviceName = (device) =>
    device.name ?? device.deviceName ?? "Unnamed device";

  const getRoomName = (device) =>
    device.roomName ?? device.room ?? "Unassigned";

  const getDevicePower = (device) =>
    Number(device.powerWatts ?? device.power ?? device.wattage ?? 0);

  const totalPowerWatts = useMemo(
    () =>
      devices.reduce(
        (total, device) =>
          total + (isDeviceOn(device) ? getDevicePower(device) : 0),
        0
      ),
    [devices]
  );

  const activeDevices = devices.filter(isDeviceOn).length;

  const totalEnergyKwh = useMemo(
    () =>
      sessions.reduce(
        (total, session) => total + Number(session.energyKwh || 0),
        0
      ),
    [sessions]
  );

  const activeSessionCount = sessions.filter(
    (session) => session.active || Number(session.endedAt) === 0
  ).length;

  const bill = useMemo(
    () => calculateBill(totalEnergyKwh, tariff),
    [totalEnergyKwh, tariff]
  );

  const filteredDevices = useMemo(() => {
    const query = search.trim().toLowerCase();

    return devices.filter((device) => {
      const matchesRoom =
        selectedRoom === "All Rooms" ||
        getRoomName(device) === selectedRoom;

      const matchesSearch =
        !query ||
        getDeviceName(device).toLowerCase().includes(query) ||
        getRoomName(device).toLowerCase().includes(query);

      return matchesRoom && matchesSearch;
    });
  }, [devices, search, selectedRoom]);

  const deviceGroups = useMemo(() => {
    const groups = {};

    filteredDevices.forEach((device) => {
      const roomName = getRoomName(device);

      if (!groups[roomName]) groups[roomName] = [];
      groups[roomName].push(device);
    });

    return groups;
  }, [filteredDevices]);

  const updateDevicePower = async (device, turnOn) => {
    const id = getDeviceId(device);

    if (id == null) {
      setError("This device does not have a valid ID.");
      return;
    }

    setUpdatingDevices((current) => ({ ...current, [id]: true }));
    setError("");

    try {
      await fetchJson(
        `${API_URL}/devices/${encodeURIComponent(id)}/power`,
        {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ on: turnOn }),
        }
      );

      await loadDashboard(false);
      await loadEnergySessions();
    } catch (err) {
      setError(`Could not update ${getDeviceName(device)}: ${err.message}`);
    } finally {
      setUpdatingDevices((current) => ({ ...current, [id]: false }));
    }
  };

  const deleteDevice = async (device) => {
    const id = getDeviceId(device);
    const name = getDeviceName(device);

    if (!id) {
      setDeviceMessage("Error: This device does not have a valid ID.");
      return;
    }

    const confirmed = window.confirm(
      `Delete "${name}" permanently? This cannot be undone.`
    );

    if (!confirmed) return;

    setDeletingDevices((current) => ({
      ...current,
      [id]: true,
    }));
    setDeviceMessage("");
    setError("");

    try {
      const response = await fetch(
        `${API_URL}/devices/${encodeURIComponent(id)}`,
        { method: "DELETE" }
      );

      if (!response.ok) {
        throw new Error(await getApiError(response));
      }

      setDeviceMessage(`"${name}" deleted successfully.`);

      await loadDashboard(false);
      await loadEnergySessions();
    } catch (err) {
      setDeviceMessage(`Error: Could not delete "${name}": ${err.message}`);
    } finally {
      setDeletingDevices((current) => ({
        ...current,
        [id]: false,
      }));
    }
  };



  const handleAddDevice = async (event) => {
    event.preventDefault();

    setDeviceMessage("");
    setError("");

    const id = deviceForm.id.trim();
    const name = deviceForm.name.trim();
    const roomName = deviceForm.roomName.trim();
    const powerWatts = Number(deviceForm.powerWatts);
    const lifetimeValue = Number(deviceForm.lifetimeValue);

    // Validate required fields
    if (!id || !name || !roomName) {
      setDeviceMessage("Error: Please fill in all required fields.");
      return;
    }

    if (!Number.isFinite(powerWatts) || powerWatts <= 0) {
      setDeviceMessage("Error: Power must be greater than zero.");
      return;
    }

    if (
      deviceForm.temporary &&
      (!Number.isFinite(lifetimeValue) || lifetimeValue <= 0)
    ) {
      setDeviceMessage("Error: Lifetime must be greater than zero.");
      return;
    }

    const lifetimeSeconds = deviceForm.temporary
      ? lifetimeValue *
      (deviceForm.lifetimeUnit === "hours" ? 3600 : 60)
      : 0;

    const payload = {
      id,
      name,
      type: deviceForm.type,
      roomName,
      powerWatts,
      temporary: deviceForm.temporary,
      lifetimeSeconds,
    };

    setDeviceBusy(true);

    try {
      const response = await fetch(`${API_URL}/devices`, {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
        },
        body: JSON.stringify(payload),
      });

      if (!response.ok) {
        throw new Error(await getApiError(response));
      }

      setDeviceMessage(`"${name}" added successfully!`);

      setDeviceForm({
        id: "",
        name: "",
        type: "Light",
        roomName: "",
        powerWatts: "60",
        temporary: true,
        lifetimeValue: "30",
        lifetimeUnit: "minutes",
      });

      await loadDashboard(false);
      await loadEnergySessions();
    } catch (err) {
      setDeviceMessage(`Error adding device: ${err.message}`);
    } finally {
      setDeviceBusy(false);
    }
  };


  const bulkToggle = async (turnOn) => {
    const targets = filteredDevices.filter(
      (device) => isDeviceOn(device) !== turnOn
    );

    if (targets.length === 0) return;

    setBulkUpdating(true);
    setError("");

    const results = await Promise.allSettled(
      targets.map((device) => {
        const id = getDeviceId(device);

        return fetchJson(
          `${API_URL}/devices/${encodeURIComponent(id)}/power`,
          {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ on: turnOn }),
          }
        );
      })
    );

    const failed = results.filter(
      (result) => result.status === "rejected"
    ).length;

    if (failed > 0) {
      setError(`${failed} device update(s) failed.`);
    }

    await loadDashboard(false);
    await loadEnergySessions();
    setBulkUpdating(false);
  };

  const handleTariffInput = (key, value) => {
    setTariffInputs((current) => ({
      ...current,
      [key]: value,
    }));
  };

  const saveTariffSettings = (event) => {
    event.preventDefault();

    const nextTariff = {};

    for (const key of Object.keys(DEFAULT_TARIFF)) {
      const value = Number(tariffInputs[key]);

      if (!Number.isFinite(value) || value < 0) {
        setError("Tariff values must be valid non-negative numbers.");
        return;
      }

      nextTariff[key] = value;
    }

    setTariff(nextTariff);
    setTariffInputs({ ...nextTariff });
    setShowTariffSettings(false);
    setError("");
  };

  const resetTariff = () => {
    setTariff(DEFAULT_TARIFF);
    setTariffInputs({ ...DEFAULT_TARIFF });
  };


  const roomMeta = (name) => {
    const lower = name.toLowerCase();
    if (lower.includes("living")) return { icon: "🛋️", tint: "violet" };
    if (lower.includes("bed")) return { icon: "🛏️", tint: "blue" };
    if (lower.includes("kitchen")) return { icon: "🍳", tint: "orange" };
    if (lower.includes("study")) return { icon: "💻", tint: "green" };
    if (lower.includes("bath")) return { icon: "🛁", tint: "pink" };
    return { icon: "🏠", tint: "violet" };
  };

  const deviceMeta = (device) => {
    const type = String(device.type ?? device.deviceType ?? "").toLowerCase();
    if (type.includes("light")) return "💡";
    if (type.includes("fan")) return "🌀";
    if (type.includes("ac") || type.includes("air")) return "❄️";
    if (type.includes("heat")) return "♨️";
    return "🔌";
  };

  const goTo = (section) => {
    setActiveSection(section);
    window.scrollTo({ top: 0, behavior: "smooth" });
  };

  const stat = (label, value, detail, icon, tint) => (
    <article className="stat-card" key={label}>
      <div className="stat-card-top">
        <span className={`stat-icon ${tint}`}>{icon}</span>
        <span className="stat-label">{label}</span>
      </div>
      <strong className="stat-value">{value}</strong>
      <p className="stat-detail">{detail}</p>
    </article>
  );

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand">
          <div className="brand-icon">⌂</div>
          <div>
            <h2>SmartHome<span>OS</span></h2>
            <p>INTELLIGENT LIVING</p>
          </div>
        </div>

        <div className="nav-label">WORKSPACE</div>
        <nav className="nav-list" aria-label="Main navigation">
          {[
            ["overview", "◫", "Overview"],
            ["devices", "⌁", "Devices"],
            ["rooms", "⌂", "Room management"],
            ["energy", "ϟ", "Energy monitor"],
            ["billing", "₹", "Bill estimator"],
            ["history", "◷", "Session history"],
          ].map(([id, icon, label]) => (
            <button key={id} className={`nav-item ${activeSection === id ? "active" : ""}`} onClick={() => goTo(id)}>
              <span className="nav-symbol">{icon}</span><span>{label}</span>
              {id === "devices" && <small>{devices.length}</small>}
            </button>
          ))}
        </nav>

        <div className="sidebar-rooms">
          <div className="nav-label">YOUR ROOMS</div>
          {rooms.map((room) => {
            const name = typeof room === "string" ? room : room.name ?? room.roomName;
            if (!name) return null;
            const meta = roomMeta(name);
            return (
              <button className="room-nav-item" key={name} onClick={() => { setSelectedRoom(name); goTo("devices"); }}>
                <span>{meta.icon}</span><span>{name}</span>
                <small>{devices.filter((d) => getRoomName(d) === name).length}</small>
              </button>
            );
          })}
        </div>

        <div className="sidebar-status">
          <span className={`connection-dot ${error ? "disconnected" : ""}`} />
          <div><strong>{error ? "API connection issue" : "Smart home connected"}</strong><p>{error ? "Check backend status" : "Local system · Live sync"}</p></div>
        </div>
      </aside>

      <main className="main-content">
        <header className="topbar">
          <div className="breadcrumb">Workspace <span>/</span> <strong>{activeSection.charAt(0).toUpperCase() + activeSection.slice(1)}</strong></div>
          <div className="topbar-actions">
            <span className="live-pill"><i /> LIVE DASHBOARD</span>
            <button className="icon-button" aria-label="Refresh dashboard" title="Refresh dashboard" onClick={() => { loadDashboard(true); loadEnergySessions(); }}>↻</button>
            <div className="avatar">S</div>

            <label className="profile-selector">
              <span>👤 User</span>
              <select
                value={selectedProfileId}
                onChange={(event) => setSelectedProfileId(event.target.value)}
              >
                <option value="">Select profile</option>
                {profiles.map((profile) => (
                  <option key={profile.id} value={profile.id}>
                    {profile.displayName}
                  </option>
                ))}
              </select>
            </label>

          </div>
        </header>

        {activeSection === "overview" && <>
          <section id="overview" className="welcome-row">
            <div>
              <p className="eyebrow">YOUR HOME AT A GLANCE</p>
              <h1>Welcome home<span>.</span></h1>
              <p className="subtitle">Control your space, monitor energy, and make everyday living simpler.</p>
            </div>
            <div className="welcome-actions">
              <div className="date-card"><span>◷</span><div><strong>{new Date().toLocaleDateString("en-IN", { weekday: "long", day: "numeric", month: "short" })}</strong><p>SmartHomeOS dashboard</p></div></div>
              <button className="primary-button" onClick={() => { setShowDeviceForm(true); goTo("devices"); }}>＋ Add device</button>
            </div>
          </section>

          {error && <div className="alert alert-error" role="alert"><span>⚠</span><div><strong>Connection notice</strong><p>{error}</p></div><button onClick={() => setError("")} aria-label="Dismiss error">×</button></div>}


          <form className="inline-create panel" onSubmit={createProfile}>
            <label className="grow-field">
              Create user profile
              <input
                value={profileNameInput}
                onChange={(event) => setProfileNameInput(event.target.value)}
                placeholder="Enter a name"
                maxLength={80}
                required
              />
            </label>

            <button
              type="submit"
              className="primary-button"
              disabled={profileBusy}
            >
              {profileBusy ? "Creating…" : "＋ Create profile"}
            </button>

            {profileMessage && (
              <p role="status">{profileMessage}</p>
            )}
          </form>

          <section className="stats-grid" aria-label="Home statistics">
            {stat("Total devices", devices.length, "Registered across your home", "⌁", "purple")}
            {stat("Devices online", `${activeDevices}/${devices.length}`, "Currently switched on", "ϟ", "green")}
            {stat("Active power", <>{(totalPowerWatts / 1000).toFixed(2)} <small>kW</small></>, `${totalPowerWatts.toLocaleString("en-IN")} watts in use`, "ϟ", "amber")}
            {stat("Rooms", rooms.length, "Configured spaces", "⌂", "blue")}
          </section>

        </>}

        {activeSection === "devices" && <section id="devices" className="content-section">
          <div className="section-heading">
            <div><p className="eyebrow">CONTROL CENTER</p><h2>Devices <span className="count-chip">{filteredDevices.length}</span></h2><p className="section-subtitle">Your connected appliances, all in one place.</p></div>
            <div className="bulk-actions">
              <button className="secondary-button" disabled={bulkUpdating} onClick={() => bulkToggle(true)}>Power all on</button>
              <button className="secondary-button" disabled={bulkUpdating} onClick={() => bulkToggle(false)}>Power all off</button>
              <button className="primary-button" onClick={() => { setShowDeviceForm((v) => !v); setDeviceMessage(""); }}>{showDeviceForm ? "Cancel" : "＋ Add device"}</button>
            </div>
          </div>

          {showDeviceForm && (
            <form className="device-form panel" onSubmit={handleAddDevice}>
              <div className="form-heading"><div><p className="eyebrow">NEW CONNECTION</p><h3>Add a device</h3></div><button type="button" className="icon-button" onClick={() => setShowDeviceForm(false)} aria-label="Close form">×</button></div>
              <div className="form-grid">
                <label>Device ID<input value={deviceForm.id} onChange={(e) => setDeviceForm({ ...deviceForm, id: e.target.value })} placeholder="e.g. BEDROOM_LIGHT_02" required /></label>
                <label>Display name<input value={deviceForm.name} onChange={(e) => setDeviceForm({ ...deviceForm, name: e.target.value })} placeholder="e.g. Bedside lamp" required /></label>
                <label>Device type<select value={deviceForm.type} onChange={(e) => setDeviceForm({ ...deviceForm, type: e.target.value })}><option>Light</option><option>Fan</option><option>AC</option><option>Heater</option><option>Other</option></select></label>
                <label>Room<select value={deviceForm.roomName} onChange={(e) => setDeviceForm({ ...deviceForm, roomName: e.target.value })} required><option value="">Select a room</option>{rooms.map((room) => { const name = typeof room === "string" ? room : room.name ?? room.roomName; return name ? <option key={name} value={name}>{name}</option> : null; })}</select></label>
                <label>Rated power (watts)<input type="number" min="1" value={deviceForm.powerWatts} onChange={(e) => setDeviceForm({ ...deviceForm, powerWatts: e.target.value })} required /></label>
                <div className="temporary-field"><label className="checkbox-label"><input type="checkbox" checked={deviceForm.temporary} onChange={(e) => setDeviceForm({ ...deviceForm, temporary: e.target.checked })} /> Temporary device</label>{deviceForm.temporary && <div className="lifetime-row"><input aria-label="Device lifetime" type="number" min="1" value={deviceForm.lifetimeValue} onChange={(e) => setDeviceForm({ ...deviceForm, lifetimeValue: e.target.value })} required /><select aria-label="Lifetime unit" value={deviceForm.lifetimeUnit} onChange={(e) => setDeviceForm({ ...deviceForm, lifetimeUnit: e.target.value })}><option value="minutes">Minutes</option><option value="hours">Hours</option></select></div>}</div>
              </div>
              {deviceMessage && <div className={`form-message ${deviceMessage.startsWith("Error") ? "is-error" : "is-success"}`} role="status">{deviceMessage}</div>}
              <div className="form-actions"><button type="button" className="secondary-button" onClick={() => setShowDeviceForm(false)}>Cancel</button><button type="submit" className="primary-button" disabled={deviceBusy}>{deviceBusy ? "Adding device…" : "Add device"}</button></div>
            </form>
          )}

          <div className="toolbar">
            <label className="search-box"><span>⌕</span><input placeholder="Search devices or rooms…" value={search} onChange={(e) => setSearch(e.target.value)} /><kbd>⌕</kbd></label>
            <label className="filter-select"><span>Room</span><select value={selectedRoom} onChange={(e) => setSelectedRoom(e.target.value)}><option>All Rooms</option>{rooms.map((room) => { const name = typeof room === "string" ? room : room.name ?? room.roomName; return name ? <option key={name} value={name}>{name}</option> : null; })}</select></label>
          </div>

          {loading ? <div className="empty-state"><div className="loading-orb" /><h3>Connecting to your home…</h3><p>Fetching the latest device status.</p></div> : Object.keys(deviceGroups).length === 0 ? <div className="empty-state"><span>⌁</span><h3>No devices found</h3><p>Try another search or add a device to get started.</p><button className="primary-button" onClick={() => setShowDeviceForm(true)}>＋ Add your first device</button></div> : Object.entries(deviceGroups).map(([roomName, roomDevices]) => {
            const meta = roomMeta(roomName);
            const onCount = roomDevices.filter(isDeviceOn).length;
            return <div className="room-section" key={roomName}>
              <div className="room-heading"><div className="room-title"><span className={`room-emoji ${meta.tint}`}>{meta.icon}</span><div><h3>{roomName}</h3><p>{onCount} of {roomDevices.length} devices active</p></div></div><button className="text-button" onClick={() => setSelectedRoom(roomName)}>View room →</button></div>
              <div className="device-grid">{roomDevices.map((device) => {
                const id = getDeviceId(device);
                const on = isDeviceOn(device);
                const busy = Boolean(updatingDevices[id]);
                const deleting = Boolean(deletingDevices[id]);
                return <article className={`device-card ${on ? "device-on" : ""}`} key={id}>
                  <div className="device-card-top"><span className={`device-icon ${on ? "icon-on" : ""}`}>{deviceMeta(device)}</span><span className={`status-tag ${on ? "status-on" : "status-off"}`}><i />{on ? "ONLINE" : "OFFLINE"}</span></div>
                  <h4>{getDeviceName(device)}</h4><p className="device-type">{device.type ?? device.deviceType ?? "Smart appliance"}</p>
                  <div className="device-details"><span>Rated power</span><strong>{getDevicePower(device)} W</strong></div>
                  <div className="device-card-bottom"><span className="device-id" title={id}>{id}</span><button type="button" role="switch" aria-checked={on} aria-label={`${on ? "Turn off" : "Turn on"} ${getDeviceName(device)}`} className={`toggle ${on ? "toggle-on" : ""}`} disabled={busy} onClick={() => updateDevicePower(device, !on)}><span /></button></div>
                  <button className="delete-device" disabled={busy || deleting} onClick={() => deleteDevice(device)}>{deleting ? "Deleting…" : "Remove device"}</button>
                </article>;
              })}</div>
            </div>;
          })}
        </section>}

        {activeSection === "rooms" && <section id="rooms" className="content-section">
          <div className="section-heading"><div><p className="eyebrow">SPACE MANAGEMENT</p><h2>Your rooms</h2><p className="section-subtitle">Organize devices by the spaces they belong to.</p></div></div>
          <form className="inline-create panel" onSubmit={createRoom}><label className="grow-field">Create a room<input value={roomNameInput} onChange={(e) => setRoomNameInput(e.target.value)} placeholder="e.g. Balcony" maxLength={60} /></label><button className="primary-button" disabled={roomBusy}>{roomBusy ? "Saving…" : "＋ Add room"}</button></form>
          {roomMessage && <div className={`form-message ${roomMessage.startsWith("Error") ? "is-error" : "is-success"}`} role="status">{roomMessage}</div>}
          <div className="room-management-grid">{rooms.map((room) => {
            const name = typeof room === "string" ? room : room.name ?? room.roomName;
            if (!name) return null;
            const count = devices.filter((d) => getRoomName(d) === name).length;
            const meta = roomMeta(name);
            return <article className="room-management-card" key={name}><span className={`room-emoji ${meta.tint}`}>{meta.icon}</span>{editingRoom === name ? <form onSubmit={renameRoom} className="rename-form"><input value={editingRoomName} onChange={(e) => setEditingRoomName(e.target.value)} autoFocus required /><div><button className="primary-button" disabled={roomBusy}>Save</button><button type="button" className="secondary-button" onClick={() => setEditingRoom(null)}>Cancel</button></div></form> : <><div className="room-management-info"><h3>{name}</h3><p>{count} {count === 1 ? "device" : "devices"}</p></div><div className="room-management-actions"><button className="secondary-button" onClick={() => { setEditingRoom(name); setEditingRoomName(name); setRoomMessage(""); }}>Rename</button><button className="secondary-button danger-button" disabled={roomBusy || count > 0} title={count ? "Remove devices from this room first" : "Delete empty room"} onClick={() => deleteRoom(name)}>Delete</button></div></>}</article>;
          })}</div>
        </section>}

        {activeSection === "energy" && <section id="energy" className="content-section">
          <div className="section-heading"><div><p className="eyebrow">ENERGY INTELLIGENCE</p><h2>Energy monitor</h2><p className="section-subtitle">Usage estimates from device power ratings and recorded run time.</p></div></div>
          {energyError && <div className="alert alert-error" role="alert">{energyError}</div>}
          <div className="stats-grid energy-stats">
            {stat("Recorded energy", `${totalEnergyKwh.toFixed(4)} kWh`, "Across recorded sessions", "ϟ", "amber")}
            {stat("Active sessions", activeSessionCount, "Devices currently consuming energy", "◷", "green")}
            {stat("Estimated bill", `₹${bill.total.toFixed(2)}`, "Using the configured tariff assumptions", "₹", "purple")}
            {stat("Live load", `${totalPowerWatts.toLocaleString("en-IN")} W`, "Combined rated power of ON devices", "⌁", "blue")}
          </div>
          <div className="panel note-panel"><span>ⓘ</span><p>These values are software estimates, not physical meter readings. Energy totals reflect the sessions returned by your backend.</p></div>
        </section>}

        {activeSection === "billing" && <section id="billing" className="content-section">
          <div className="section-heading"><div><p className="eyebrow">COST PLANNING</p><h2>Electricity bill estimator</h2><p className="section-subtitle">An editable, illustrative slab-based estimate — not an official utility bill.</p></div><button className="secondary-button" onClick={() => setShowTariffSettings((v) => !v)}>{showTariffSettings ? "Hide tariff settings" : "⚙ Edit tariff"}</button></div>
          {showTariffSettings && <form className="panel tariff-form" onSubmit={saveTariffSettings}><div className="form-grid">{[["slab1", "First 100 units (₹/unit)"], ["slab2", "101–300 units (₹/unit)"], ["slab3", "301–500 units (₹/unit)"], ["slab4", "Above 500 units (₹/unit)"], ["wheeling", "Wheeling charge (₹/unit)"], ["fixedCharge", "Fixed charge (₹/period)"], ["dutyPercent", "Electricity duty (%)"]].map(([key, label]) => <label key={key}>{label}<input type="number" min="0" step="0.01" value={tariffInputs[key]} onChange={(e) => handleTariffInput(key, e.target.value)} required /></label>)}</div><div className="form-actions"><button type="button" className="secondary-button" onClick={resetTariff}>Reset defaults</button><button className="primary-button">Apply rates</button></div><p className="fine-print">Tariff values are held in React state and reset on page reload. Verify the assumptions for your consumer category.</p></form>}
          <div className="billing-layout"><div className="bill-total-card"><p>ESTIMATED TOTAL</p><strong>₹{bill.total.toFixed(2)}</strong><span>Based on {bill.units.toFixed(4)} recorded units</span><div className="bill-total-foot"><span>Energy charges</span><strong>₹{bill.energyCharge.toFixed(2)}</strong></div></div><div className="panel bill-breakdown"><h3>Charge breakdown</h3>{[["First 100 units", bill.first * tariff.slab1], ["101–300 units", bill.second * tariff.slab2], ["301–500 units", bill.third * tariff.slab3], ["Above 500 units", bill.fourth * tariff.slab4], ["Wheeling charges", bill.wheelingCharge], ["Fixed charge", bill.fixedCharge], ["Electricity duty", bill.duty]].map(([label, value]) => <div className="bill-row" key={label}><span>{label}</span><strong>₹{Number(value).toFixed(2)}</strong></div>)}<div className="bill-row bill-row-total"><span>Estimated total</span><strong>₹{bill.total.toFixed(2)}</strong></div></div></div>
          <p className="fine-print">The default slab rates, fixed charge and duty are assumptions for demonstration. This estimate does not apply a calendar billing-period filter and may omit charges or adjustments on an actual bill.</p>
        </section>}

        {activeSection === "history" && <section id="history" className="content-section">
          <div className="section-heading"><div><p className="eyebrow">ACTIVITY LOG</p><h2>Energy session history</h2><p className="section-subtitle">A record of device activity reported by the backend.</p></div><span className="count-chip">{sessions.length} records</span></div>
          {energyLoading ? <div className="panel">Loading energy history…</div> : sessions.length === 0 ? <div className="empty-state"><span>◷</span><h3>No sessions yet</h3><p>Turn on a device to begin tracking its estimated energy use.</p></div> : <div className="panel table-panel"><div className="table-wrap"><table><thead><tr><th>Device</th><th>Room</th><th>Power</th><th>Duration</th><th>Energy</th><th>Status</th><th>Started</th></tr></thead><tbody>{[...sessions].sort((a, b) => Number(b.startedAt || 0) - Number(a.startedAt || 0)).map((session, index) => { const active = session.active || Number(session.endedAt) === 0; const duration = active ? Math.max(0, Math.floor(Date.now() / 1000) - Number(session.startedAt || 0)) : Math.max(0, Number(session.endedAt || 0) - Number(session.startedAt || 0)); return <tr key={session.id ?? `${session.deviceId}-${index}`}><td><strong>{session.deviceName ?? "Unknown device"}</strong></td><td>{session.roomName ?? "—"}</td><td>{Number(session.powerWatts || 0)} W</td><td>{formatDuration(duration)}</td><td>{Number(session.energyKwh || 0).toFixed(5)} kWh</td><td><span className={`status-tag ${active ? "status-on" : "status-off"}`}>{active ? "ACTIVE" : "COMPLETED"}</span></td><td>{formatDate(Number(session.startedAt || 0))}</td></tr>; })}</tbody></table></div></div>}
        </section>}

        <footer className="footer"><span>SmartHomeOS <span className="footer-dot">●</span> Built for smarter everyday living</span><span>Energy readings are software estimates · Local API</span></footer>
      </main>
    </div>
  );
}

export default App;
