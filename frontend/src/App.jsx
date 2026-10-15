
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

  const fetchJson = async (url, options = {}) => {
    const response = await fetch(url, options);

    if (!response.ok) {
      throw new Error(`Request failed (${response.status})`);
    }

    return response.json();
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

    const interval = setInterval(() => {
      loadDashboard(false);
      loadEnergySessions();
    }, 10000);

    return () => clearInterval(interval);
  }, [loadDashboard, loadEnergySessions]);

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

  const styles = {
    page: {
      minHeight: "100vh",
      padding: "24px",
      background: "var(--bg, #0b1120)",
      color: "var(--text, #e5e7eb)",
      fontFamily: "Inter, system-ui, sans-serif",
      boxSizing: "border-box",
    },
    container: {
      maxWidth: "1400px",
      margin: "0 auto",
    },
    header: {
      display: "flex",
      flexWrap: "wrap",
      alignItems: "center",
      justifyContent: "space-between",
      gap: "16px",
      marginBottom: "24px",
    },
    title: {
      margin: 0,
      fontSize: "clamp(26px, 4vw, 36px)",
    },
    subtitle: {
      margin: "8px 0 0",
      color: "var(--muted, #94a3b8)",
    },
    button: {
      border: "1px solid var(--border, #334155)",
      background: "var(--card-bg, #111827)",
      color: "var(--text, #e5e7eb)",
      padding: "10px 14px",
      borderRadius: "10px",
      cursor: "pointer",
      fontWeight: 600,
    },
    primaryButton: {
      border: "none",
      background: "#6366f1",
      color: "#fff",
      padding: "10px 14px",
      borderRadius: "10px",
      cursor: "pointer",
      fontWeight: 600,
    },
    grid: {
      display: "grid",
      gridTemplateColumns: "repeat(auto-fit, minmax(190px, 1fr))",
      gap: "16px",
      marginBottom: "24px",
    },
    panel: {
      background: "var(--card-bg, #111827)",
      border: "1px solid var(--border, #263244)",
      borderRadius: "16px",
      padding: "20px",
      marginBottom: "24px",
      minWidth: 0,
    },
    statLabel: {
      color: "var(--muted, #94a3b8)",
      fontSize: "14px",
    },
    statValue: {
      fontSize: "28px",
      fontWeight: 750,
      marginTop: "10px",
      overflowWrap: "anywhere",
    },
    input: {
      width: "100%",
      boxSizing: "border-box",
      padding: "10px 12px",
      border: "1px solid var(--border, #334155)",
      borderRadius: "9px",
      background: "var(--bg, #0b1120)",
      color: "var(--text, #e5e7eb)",
    },
    sectionTitle: {
      margin: "0 0 16px",
      fontSize: "21px",
    },
    tableWrap: {
      overflowX: "auto",
      width: "100%",
    },
    table: {
      borderCollapse: "collapse",
      width: "100%",
      minWidth: "760px",
      fontSize: "14px",
    },
    th: {
      textAlign: "left",
      padding: "12px 10px",
      borderBottom: "1px solid var(--border, #334155)",
      color: "var(--muted, #94a3b8)",
      whiteSpace: "nowrap",
    },
    td: {
      padding: "12px 10px",
      borderBottom: "1px solid var(--border, #263244)",
      whiteSpace: "nowrap",
    },
    muted: {
      color: "var(--muted, #94a3b8)",
    },
    badge: {
      display: "inline-block",
      borderRadius: "999px",
      padding: "4px 9px",
      fontSize: "12px",
      fontWeight: 700,
    },
  };

  const statCard = (label, value, detail) => (
    <div key={label} style={styles.panel}>
      <div style={styles.statLabel}>{label}</div>
      <div style={styles.statValue}>{value}</div>
      {detail && (
        <div style={{ ...styles.muted, fontSize: "13px", marginTop: "8px" }}>
          {detail}
        </div>
      )}
    </div>
  );

  return (
    <div style={styles.page}>
      <div style={styles.container}>
        <header style={styles.header}>
          <div>
            <h1 style={styles.title}>SmartHomeOS</h1>
            <p style={styles.subtitle}>
              Smart control, energy monitoring and electricity bill estimates
            </p>
          </div>

          <button
            style={styles.button}
            onClick={() => {
              loadDashboard(true);
              loadEnergySessions();
            }}
          >
            ↻ Refresh dashboard
          </button>
        </header>

        {error && (
          <div
            role="alert"
            style={{
              ...styles.panel,
              borderColor: "#ef4444",
              color: "#fca5a5",
            }}
          >
            {error}
          </div>
        )}

        {loading ? (
          <div style={styles.panel}>Loading your smart home...</div>
        ) : (
          <>
            <div style={styles.grid}>
              {statCard("Total devices", devices.length, "Across your home")}
              {statCard(
                "Devices running",
                `${activeDevices} / ${devices.length}`,
                "Currently switched ON"
              )}
              {statCard(
                "Active power",
                `${(totalPowerWatts / 1000).toFixed(3)} kW`,
                `${totalPowerWatts.toLocaleString("en-IN")} watts`
              )}
              {statCard("Rooms", rooms.length, "Configured rooms")}
            </div>


            <section style={styles.panel}>
              <h2 style={styles.sectionTitle}>Room Management</h2>

              <p style={styles.muted}>
                Create rooms, rename existing rooms, or remove rooms that have no devices.
              </p>

              <form
                onSubmit={createRoom}
                style={{
                  display: "flex",
                  gap: "10px",
                  flexWrap: "wrap",
                  marginBottom: "18px",
                }}
              >
                <input
                  style={{ ...styles.input, flex: "1 1 220px" }}
                  placeholder="Enter new room name"
                  value={roomNameInput}
                  onChange={(event) => setRoomNameInput(event.target.value)}
                  maxLength={60}
                  disabled={roomBusy}
                />

                <button
                  type="submit"
                  style={styles.primaryButton}
                  disabled={roomBusy}
                >
                  {roomBusy ? "Please wait..." : "＋ Add Room"}
                </button>
              </form>

              {roomMessage && (
                <p
                  role="status"
                  style={{
                    color: roomMessage.startsWith("Error:")
                      ? "#fca5a5"
                      : "#4ade80",
                    overflowWrap: "anywhere",
                  }}
                >
                  {roomMessage}
                </p>
              )}

              {rooms.length === 0 ? (
                <p style={styles.muted}>No rooms configured yet.</p>
              ) : (
                <div style={styles.grid}>
                  {rooms.map((room) => {
                    const name =
                      typeof room === "string"
                        ? room
                        : room.name ?? room.roomName;

                    if (!name) return null;

                    const count = devices.filter(
                      (device) => getRoomName(device) === name
                    ).length;

                    const isEditing = editingRoom === name;

                    return (
                      <div
                        key={name}
                        style={{
                          ...styles.panel,
                          marginBottom: 0,
                        }}
                      >
                        {isEditing ? (
                          <form onSubmit={renameRoom}>
                            <label
                              style={{
                                display: "block",
                                marginBottom: "8px",
                              }}
                            >
                              New room name
                            </label>

                            <input
                              style={styles.input}
                              value={editingRoomName}
                              onChange={(event) =>
                                setEditingRoomName(event.target.value)
                              }
                              maxLength={60}
                              autoFocus
                              disabled={roomBusy}
                            />

                            <div
                              style={{
                                display: "flex",
                                gap: "8px",
                                flexWrap: "wrap",
                                marginTop: "12px",
                              }}
                            >
                              <button
                                type="submit"
                                style={styles.primaryButton}
                                disabled={roomBusy}
                              >
                                Save
                              </button>

                              <button
                                type="button"
                                style={styles.button}
                                disabled={roomBusy}
                                onClick={() => {
                                  setEditingRoom(null);
                                  setEditingRoomName("");
                                }}
                              >
                                Cancel
                              </button>
                            </div>
                          </form>
                        ) : (
                          <>
                            <h3 style={{ marginTop: 0, overflowWrap: "anywhere" }}>
                              {name}
                            </h3>

                            <p style={styles.muted}>
                              {count} {count === 1 ? "device" : "devices"}
                            </p>

                            <div
                              style={{
                                display: "flex",
                                gap: "8px",
                                flexWrap: "wrap",
                              }}
                            >
                              <button
                                type="button"
                                style={styles.button}
                                disabled={roomBusy}
                                onClick={() => {
                                  setEditingRoom(name);
                                  setEditingRoomName(name);
                                  setRoomMessage("");
                                }}
                              >
                                Rename
                              </button>

                              <button
                                type="button"
                                style={{
                                  ...styles.button,
                                  borderColor: "#ef4444",
                                  color: "#fca5a5",
                                }}
                                disabled={roomBusy || count > 0}
                                title={
                                  count > 0
                                    ? "Remove all devices from this room first."
                                    : "Delete this empty room"
                                }
                                onClick={() => deleteRoom(name)}
                              >
                                Delete
                              </button>
                            </div>

                            {count > 0 && (
                              <p style={{ ...styles.muted, fontSize: "12px" }}>
                                Remove this room's devices before deleting it.
                              </p>
                            )}
                          </>
                        )}
                      </div>
                    );
                  })}
                </div>
              )}
            </section>

            <section style={styles.panel}>
              <h2 style={styles.sectionTitle}>Energy Monitor</h2>

              {energyError && (
                <p style={{ color: "#fca5a5" }}>{energyError}</p>
              )}

              <div style={styles.grid}>
                {statCard(
                  "Recorded energy",
                  `${totalEnergyKwh.toFixed(4)} kWh`,
                  "Sum of recorded energy sessions"
                )}
                {statCard(
                  "Active sessions",
                  activeSessionCount,
                  "Devices with ongoing sessions"
                )}
                {statCard(
                  "Estimated bill",
                  `₹${bill.total.toFixed(2)}`,
                  "Based on current tariff settings"
                )}
              </div>

              <p style={styles.muted}>
                Energy usage is estimated from rated device power and operating
                time. It is not a physical meter reading.
              </p>
            </section>

            <section style={styles.panel}>
              <div
                style={{
                  display: "flex",
                  justifyContent: "space-between",
                  alignItems: "center",
                  flexWrap: "wrap",
                  gap: "12px",
                  marginBottom: "16px",
                }}
              >
                <h2 style={{ ...styles.sectionTitle, margin: 0 }}>
                  Electricity Bill Estimator
                </h2>

                <button
                  style={styles.button}
                  onClick={() => setShowTariffSettings((value) => !value)}
                >
                  {showTariffSettings ? "Hide tariff settings" : "Edit tariff"}
                </button>
              </div>

              <p style={styles.muted}>
                MSEDCL-inspired residential slab assumptions. The calculation
                below applies each energy rate progressively to the units
                recorded by SmartHomeOS.
              </p>

              {showTariffSettings && (
                <form
                  onSubmit={saveTariffSettings}
                  style={{
                    border: "1px solid var(--border, #334155)",
                    borderRadius: "12px",
                    padding: "16px",
                    margin: "18px 0",
                  }}
                >
                  <h3 style={{ marginTop: 0 }}>Tariff settings</h3>

                  <div style={styles.grid}>
                    {[
                      ["slab1", "First 100 units (₹/unit)"],
                      ["slab2", "101–300 units (₹/unit)"],
                      ["slab3", "301–500 units (₹/unit)"],
                      ["slab4", "Above 500 units (₹/unit)"],
                      ["wheeling", "Wheeling charge (₹/unit)"],
                      ["fixedCharge", "Fixed charge (₹/billing period)"],
                      ["dutyPercent", "Electricity duty (%)"],
                    ].map(([key, label]) => (
                      <label key={key} style={{ display: "block" }}>
                        <span
                          style={{
                            display: "block",
                            marginBottom: "7px",
                            fontSize: "13px",
                          }}
                        >
                          {label}
                        </span>
                        <input
                          style={styles.input}
                          type="number"
                          min="0"
                          step="0.01"
                          value={tariffInputs[key]}
                          onChange={(event) =>
                            handleTariffInput(key, event.target.value)
                          }
                          required
                        />
                      </label>
                    ))}
                  </div>

                  <div style={{ display: "flex", gap: "10px", flexWrap: "wrap" }}>
                    <button type="submit" style={styles.primaryButton}>
                      Apply rates
                    </button>
                    <button
                      type="button"
                      style={styles.button}
                      onClick={resetTariff}
                    >
                      Reset defaults
                    </button>
                  </div>

                  <p style={{ ...styles.muted, fontSize: "12px" }}>
                    These settings currently remain in React state and reset
                    when the page reloads. Saving them to SQLite is a separate
                    next step.
                  </p>
                </form>
              )}

              <div style={styles.grid}>
                {statCard(
                  "Recorded units",
                  `${bill.units.toFixed(4)} kWh`,
                  "1 kWh equals 1 electricity unit"
                )}
                {statCard(
                  "Energy charges",
                  `₹${bill.energyCharge.toFixed(2)}`,
                  "Calculated progressively by slab"
                )}
                {statCard(
                  "Additional charges",
                  `₹${(bill.wheelingCharge + bill.fixedCharge + bill.duty).toFixed(2)}`,
                  "Wheeling + fixed charge + duty"
                )}
                {statCard(
                  "Estimated total",
                  `₹${bill.total.toFixed(2)}`,
                  "For the configured billing period"
                )}
              </div>

              <h3>Charge breakdown</h3>

              <div style={styles.tableWrap}>
                <table style={styles.table}>
                  <thead>
                    <tr>
                      <th style={styles.th}>Charge</th>
                      <th style={styles.th}>Calculation</th>
                      <th style={styles.th}>Amount</th>
                    </tr>
                  </thead>
                  <tbody>
                    <tr>
                      <td style={styles.td}>First 100 units</td>
                      <td style={styles.td}>
                        {bill.first.toFixed(4)} × ₹{tariff.slab1.toFixed(2)}
                      </td>
                      <td style={styles.td}>
                        ₹{(bill.first * tariff.slab1).toFixed(2)}
                      </td>
                    </tr>
                    <tr>
                      <td style={styles.td}>101–300 units</td>
                      <td style={styles.td}>
                        {bill.second.toFixed(4)} × ₹{tariff.slab2.toFixed(2)}
                      </td>
                      <td style={styles.td}>
                        ₹{(bill.second * tariff.slab2).toFixed(2)}
                      </td>
                    </tr>
                    <tr>
                      <td style={styles.td}>301–500 units</td>
                      <td style={styles.td}>
                        {bill.third.toFixed(4)} × ₹{tariff.slab3.toFixed(2)}
                      </td>
                      <td style={styles.td}>
                        ₹{(bill.third * tariff.slab3).toFixed(2)}
                      </td>
                    </tr>
                    <tr>
                      <td style={styles.td}>Above 500 units</td>
                      <td style={styles.td}>
                        {bill.fourth.toFixed(4)} × ₹{tariff.slab4.toFixed(2)}
                      </td>
                      <td style={styles.td}>
                        ₹{(bill.fourth * tariff.slab4).toFixed(2)}
                      </td>
                    </tr>
                    <tr>
                      <td style={styles.td}>Wheeling charges</td>
                      <td style={styles.td}>
                        {bill.units.toFixed(4)} × ₹{tariff.wheeling.toFixed(2)}
                      </td>
                      <td style={styles.td}>
                        ₹{bill.wheelingCharge.toFixed(2)}
                      </td>
                    </tr>
                    <tr>
                      <td style={styles.td}>Fixed charge</td>
                      <td style={styles.td}>Configured amount</td>
                      <td style={styles.td}>
                        ₹{bill.fixedCharge.toFixed(2)}
                      </td>
                    </tr>
                    <tr>
                      <td style={styles.td}>Electricity duty</td>
                      <td style={styles.td}>
                        {tariff.dutyPercent}% of energy + wheeling charges
                      </td>
                      <td style={styles.td}>₹{bill.duty.toFixed(2)}</td>
                    </tr>
                    <tr>
                      <td style={{ ...styles.td, fontWeight: 800 }}>
                        Estimated total
                      </td>
                      <td style={styles.td}>All charges added</td>
                      <td style={{ ...styles.td, fontWeight: 800 }}>
                        ₹{bill.total.toFixed(2)}
                      </td>
                    </tr>
                  </tbody>
                </table>
              </div>

              <p style={{ ...styles.muted, fontSize: "12px", marginTop: "16px" }}>
                This is an illustrative estimate, not an official MSEDCL bill.
                The configured ₹100 fixed charge and 16% duty are assumptions
                that you should verify for your consumer category. Actual bills
                can include other charges, credits, taxes and adjustments.
                This calculation uses all recorded sessions and does not yet
                filter energy by a calendar billing period.
              </p>
            </section>

            <section style={styles.panel}>
              <h2 style={styles.sectionTitle}>Rooms and Devices</h2>

              <button
                style={styles.button}
                onClick={() => {
                  setShowDeviceForm((current) => !current);
                  setDeviceMessage("");
                }}
              >
                {showDeviceForm ? "Cancel" : "+ Add Device"}
              </button>

              {showDeviceForm && (
                <div style={{ ...styles.panel, marginTop: "16px" }}>
                  <h3>Add a New Device</h3>

                  <form
                    onSubmit={handleAddDevice}
                    style={{
                      display: "grid",
                      gap: "12px",
                      maxWidth: "500px",
                    }}
                  >
                    <input
                      placeholder="Device ID (e.g. BEDROOM_LIGHT_02)"
                      value={deviceForm.id}
                      onChange={(e) =>
                        setDeviceForm({ ...deviceForm, id: e.target.value })
                      }
                      required
                    />

                    <input
                      placeholder="Device Name (e.g. Bedroom Light)"
                      value={deviceForm.name}
                      onChange={(e) =>
                        setDeviceForm({ ...deviceForm, name: e.target.value })
                      }
                      required
                    />

                    <label>
                      Device Type
                      <select
                        value={deviceForm.type}
                        onChange={(e) =>
                          setDeviceForm({ ...deviceForm, type: e.target.value })
                        }
                      >
                        <option value="Light">Light</option>
                        <option value="Fan">Fan</option>
                        <option value="AC">AC</option>
                        <option value="Heater">Heater</option>
                        <option value="Other">Other</option>
                      </select>
                    </label>

                    <label>
                      Room
                      <select
                        value={deviceForm.roomName}
                        onChange={(e) =>
                          setDeviceForm({ ...deviceForm, roomName: e.target.value })
                        }
                        required
                      >
                        <option value="">Select a room</option>
                        {rooms.map((room) => {
                          const name =
                            typeof room === "string"
                              ? room
                              : room.name || room.roomName;

                          return name ? (
                            <option key={name} value={name}>
                              {name}
                            </option>
                          ) : null;
                        })}
                      </select>
                    </label>

                    <label>
                      Power Consumption (Watts)
                      <input
                        type="number"
                        min="1"
                        value={deviceForm.powerWatts}
                        onChange={(e) =>
                          setDeviceForm({
                            ...deviceForm,
                            powerWatts: e.target.value,
                          })
                        }
                        required
                      />
                    </label>

                    <label>
                      <input
                        type="checkbox"
                        checked={deviceForm.temporary}
                        onChange={(e) =>
                          setDeviceForm({
                            ...deviceForm,
                            temporary: e.target.checked,
                          })
                        }
                      />
                      Temporary device
                    </label>

                    {deviceForm.temporary && (
                      <div style={{ display: "flex", gap: "8px" }}>
                        <input
                          type="number"
                          min="1"
                          value={deviceForm.lifetimeValue}
                          onChange={(e) =>
                            setDeviceForm({
                              ...deviceForm,
                              lifetimeValue: e.target.value,
                            })
                          }
                          required
                        />

                        <select
                          value={deviceForm.lifetimeUnit}
                          onChange={(e) =>
                            setDeviceForm({
                              ...deviceForm,
                              lifetimeUnit: e.target.value,
                            })
                          }
                        >
                          <option value="minutes">Minutes</option>
                          <option value="hours">Hours</option>
                        </select>
                      </div>
                    )}

                    <button type="submit" style={styles.button} disabled={deviceBusy}>
                      {deviceBusy ? "Adding..." : "Add Device"}
                    </button>

                    {deviceMessage && <p>{deviceMessage}</p>}
                  </form>
                </div>
              )}

              <div
                style={{
                  display: "grid",
                  gridTemplateColumns: "repeat(auto-fit, minmax(200px, 1fr))",
                  gap: "12px",
                  marginBottom: "18px",
                }}
              >
                <label>
                  <span style={{ display: "block", marginBottom: "7px" }}>
                    Search devices
                  </span>
                  <input
                    style={styles.input}
                    placeholder="Search device or room..."
                    value={search}
                    onChange={(event) => setSearch(event.target.value)}
                  />
                </label>

                <label>
                  <span style={{ display: "block", marginBottom: "7px" }}>
                    Filter by room
                  </span>
                  <select
                    style={styles.input}
                    value={selectedRoom}
                    onChange={(event) => setSelectedRoom(event.target.value)}
                  >
                    <option value="All Rooms">All Rooms</option>
                    {rooms.map((room) => {
                      const roomName =
                        typeof room === "string"
                          ? room
                          : room.name ?? room.roomName;

                      return roomName ? (
                        <option key={roomName} value={roomName}>
                          {roomName}
                        </option>
                      ) : null;
                    })}
                  </select>
                </label>
              </div>

              <div
                style={{
                  display: "flex",
                  gap: "10px",
                  flexWrap: "wrap",
                  marginBottom: "22px",
                }}
              >
                <button
                  style={styles.primaryButton}
                  disabled={bulkUpdating}
                  onClick={() => bulkToggle(true)}
                >
                  Turn filtered devices ON
                </button>
                <button
                  style={styles.button}
                  disabled={bulkUpdating}
                  onClick={() => bulkToggle(false)}
                >
                  Turn filtered devices OFF
                </button>
              </div>

              {Object.keys(deviceGroups).length === 0 ? (
                <p style={styles.muted}>No devices match your search.</p>
              ) : (
                Object.entries(deviceGroups).map(([roomName, roomDevices]) => (
                  <div key={roomName} style={{ marginBottom: "24px" }}>
                    <h3 style={{ marginBottom: "12px" }}>{roomName}</h3>

                    <div style={styles.grid}>
                      {roomDevices.map((device) => {
                        const id = getDeviceId(device);
                        const on = isDeviceOn(device);
                        const power = getDevicePower(device);
                        const busy = Boolean(updatingDevices[id]);

                        return (
                          <div
                            key={id}
                            style={{
                              ...styles.panel,
                              marginBottom: 0,
                              borderColor: on ? "#6366f1" : undefined,
                            }}
                          >
                            <div
                              style={{
                                display: "flex",
                                justifyContent: "space-between",
                                gap: "10px",
                                alignItems: "flex-start",
                              }}
                            >
                              <div>
                                <h4 style={{ margin: "0 0 8px" }}>
                                  {getDeviceName(device)}
                                </h4>
                                <p style={{ ...styles.muted, margin: 0 }}>
                                  {power} W rated power
                                </p>
                              </div>

                              <span
                                style={{
                                  ...styles.badge,
                                  background: on
                                    ? "rgba(34,197,94,0.15)"
                                    : "rgba(148,163,184,0.12)",
                                  color: on ? "#4ade80" : "#94a3b8",
                                }}
                              >
                                {on ? "ON" : "OFF"}
                              </span>
                            </div>


                            <button
                              type="button"
                              style={{
                                ...(on ? styles.button : styles.primaryButton),
                                width: "100%",
                                marginTop: "18px",
                                opacity: busy ? 0.6 : 1,
                              }}
                              disabled={busy}
                              onClick={() => updateDevicePower(device, !on)}
                            >
                              {busy
                                ? "Updating..."
                                : on
                                  ? "Turn OFF"
                                  : "Turn ON"}
                            </button>

                            <button
                              type="button"
                              style={{
                                ...styles.button,
                                background: "#7f1d1d",
                                marginTop: "8px",
                                width: "100%",
                              }}
                              disabled={busy || Boolean(deletingDevices[id])}
                              onClick={() => deleteDevice(device)}
                            >
                              {deletingDevices[id] ? "Deleting..." : "Delete Device"}
                            </button>

                          </div>
                        );
                      })}
                    </div>
                  </div>
                ))
              )}
            </section>

            <section style={styles.panel}>
              <h2 style={styles.sectionTitle}>Energy Session History</h2>

              {energyLoading ? (
                <p style={styles.muted}>Loading energy sessions...</p>
              ) : sessions.length === 0 ? (
                <p style={styles.muted}>
                  No energy sessions recorded yet. Turn a device ON to start
                  tracking its estimated usage.
                </p>
              ) : (
                <div style={styles.tableWrap}>
                  <table style={styles.table}>
                    <thead>
                      <tr>
                        <th style={styles.th}>Device</th>
                        <th style={styles.th}>Room</th>
                        <th style={styles.th}>Power</th>
                        <th style={styles.th}>Duration</th>
                        <th style={styles.th}>Energy</th>
                        <th style={styles.th}>Status</th>
                        <th style={styles.th}>Started</th>
                      </tr>
                    </thead>
                    <tbody>
                      {[...sessions]
                        .sort(
                          (a, b) =>
                            Number(b.startedAt || 0) -
                            Number(a.startedAt || 0)
                        )
                        .map((session, index) => {
                          const active =
                            session.active || Number(session.endedAt) === 0;

                          const durationSeconds = active
                            ? Math.max(
                              0,
                              Math.floor(Date.now() / 1000) -
                              Number(session.startedAt || 0)
                            )
                            : Math.max(
                              0,
                              Number(session.endedAt || 0) -
                              Number(session.startedAt || 0)
                            );

                          return (
                            <tr
                              key={session.id ?? `${session.deviceId}-${index}`}
                            >
                              <td style={styles.td}>
                                {session.deviceName ?? "Unknown device"}
                              </td>
                              <td style={styles.td}>
                                {session.roomName ?? "—"}
                              </td>
                              <td style={styles.td}>
                                {Number(session.powerWatts || 0)} W
                              </td>
                              <td style={styles.td}>
                                {formatDuration(durationSeconds)}
                              </td>
                              <td style={styles.td}>
                                {Number(session.energyKwh || 0).toFixed(5)} kWh
                              </td>
                              <td style={styles.td}>
                                <span
                                  style={{
                                    ...styles.badge,
                                    background: active
                                      ? "rgba(34,197,94,0.15)"
                                      : "rgba(148,163,184,0.12)",
                                    color: active ? "#4ade80" : "#94a3b8",
                                  }}
                                >
                                  {active ? "ACTIVE" : "COMPLETED"}
                                </span>
                              </td>
                              <td style={styles.td}>
                                {formatDate(Number(session.startedAt || 0))}
                              </td>
                            </tr>
                          );
                        })}
                    </tbody>
                  </table>
                </div>
              )}
            </section>
          </>
        )}

        <footer
          style={{
            ...styles.muted,
            fontSize: "12px",
            textAlign: "center",
            padding: "10px 0 24px",
          }}
        >
          SmartHomeOS · Energy values are software estimates, not meter readings.
        </footer>
      </div>
    </div>
  );
}

export default App;
