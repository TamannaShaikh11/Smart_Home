
import { useCallback, useEffect, useMemo, useState } from "react";
import "./App.css";

const API_URL = "http://localhost:8080/api";

const roomIcons = {
  "Living Room": "🛋️",
  Bedroom: "🛏️",
  Kitchen: "🍳",
  "Study Room": "📚",
  Bathroom: "🚿",
  "Guest Room": "🛌",
};

const typeIcons = {
  Light: "💡",
  Fan: "🌀",
  AC: "❄️",
  Heater: "♨️",
  Other: "🔌",
};

function App() {
  const [rooms, setRooms] = useState([]);
  const [devices, setDevices] = useState([]);
  const [selectedRoom, setSelectedRoom] = useState("All Rooms");
  const [search, setSearch] = useState("");
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState("");
  const [busyDevices, setBusyDevices] = useState([]);
  const [bulkBusy, setBulkBusy] = useState(false);

  // Load real rooms and devices from the C++ API.
  const loadData = useCallback(async (showLoading = false) => {
    if (showLoading) setLoading(true);

    try {
      const [roomsResponse, devicesResponse] = await Promise.all([
        fetch(`${API_URL}/rooms`),
        fetch(`${API_URL}/devices`),
      ]);

      if (!roomsResponse.ok || !devicesResponse.ok) {
        throw new Error("The SmartHomeOS API returned an error.");
      }

      const [roomData, deviceData] = await Promise.all([
        roomsResponse.json(),
        devicesResponse.json(),
      ]);

      if (!Array.isArray(roomData) || !Array.isArray(deviceData)) {
        throw new Error("The API returned an unexpected data format.");
      }

      setRooms(
        roomData.map((room) => ({
          name: room.name,
          icon: roomIcons[room.name] || "🏠",
        }))
      );

      setDevices(
        deviceData.map((device) => ({
          ...device,
          power: Number(device.powerWatts) || 0,
          on: device.status === "ON",
        }))
      );

      setError("");
    } catch (err) {
      setError(
        "Cannot connect to SmartHomeOS. Make sure SmartHomeAPI.exe is running on port 8080."
      );
    } finally {
      if (showLoading) setLoading(false);
    }
  }, []);

  useEffect(() => {
    loadData(true);
  }, [loadData]);

  const allDevices = devices;

  const onCount = allDevices.filter((device) => device.on).length;

  const totalPower = allDevices
    .filter((device) => device.on)
    .reduce((sum, device) => sum + device.power, 0);

  const visibleRooms = useMemo(() => {
    return rooms
      .filter(
        (room) =>
          selectedRoom === "All Rooms" || room.name === selectedRoom
      )
      .map((room) => ({
        ...room,
        devices: devices
          .filter((device) => device.roomName === room.name)
          .filter((device) =>
            `${device.name} ${device.type} ${device.id} ${device.roomName}`
              .toLowerCase()
              .includes(search.toLowerCase())
          ),
      }))
      .filter((room) => room.devices.length > 0);
  }, [rooms, devices, selectedRoom, search]);

  // Ask the C++ API to change the actual device state.
  async function toggleDevice(deviceId, turnOn) {
    if (busyDevices.includes(deviceId) || bulkBusy) return;

    setBusyDevices((current) => [...current, deviceId]);
    setError("");

    try {
      const response = await fetch(
        `${API_URL}/devices/${encodeURIComponent(deviceId)}/power`,
        {
          method: "POST",
          headers: {
            "Content-Type": "application/json",
          },
          body: JSON.stringify({ on: turnOn }),
        }
      );

      const result = await response.json();

      if (!response.ok) {
        throw new Error(result.error || "Could not update the device.");
      }

      // Read back the saved state from the backend.
      await loadData();
    } catch (err) {
      setError(err.message || "Failed to update the device.");
    } finally {
      setBusyDevices((current) =>
        current.filter((id) => id !== deviceId)
      );
    }
  }

  // Bulk actions use the existing individual-device API.
  async function toggleAll(turnOn) {
    if (bulkBusy || busyDevices.length > 0) return;

    setBulkBusy(true);
    setError("");

    try {
      const results = await Promise.all(
        allDevices.map(async (device) => {
          const response = await fetch(
            `${API_URL}/devices/${encodeURIComponent(device.id)}/power`,
            {
              method: "POST",
              headers: {
                "Content-Type": "application/json",
              },
              body: JSON.stringify({ on: turnOn }),
            }
          );

          if (!response.ok) {
            const result = await response.json().catch(() => ({}));
            throw new Error(
              `${device.name}: ${result.error || "request failed"}`
            );
          }

          return device.id;
        })
      );

      await loadData();

      if (results.length !== allDevices.length) {
        setError("Some devices may not have been updated.");
      }
    } catch (err) {
      setError(
        `${err.message} Refreshing device states from the backend.`
      );
      await loadData();
    } finally {
      setBulkBusy(false);
    }
  }

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand">
          <div className="brand-icon">⌂</div>
          <div>
            <h2>
              SmartHome<span>OS</span>
            </h2>
            <p>HOME CONTROL CENTER</p>
          </div>
        </div>

        <div className="nav-label">WORKSPACE</div>

        <button
          className={`nav-item ${
            selectedRoom === "All Rooms" ? "active" : ""
          }`}
          onClick={() => setSelectedRoom("All Rooms")}
        >
          <span>▦</span> Overview
        </button>

        <div className="nav-label room-label">YOUR ROOMS</div>

        {rooms.map((room) => (
          <button
            key={room.name}
            className={`nav-item ${
              selectedRoom === room.name ? "active" : ""
            }`}
            onClick={() => setSelectedRoom(room.name)}
          >
            <span>{room.icon}</span>
            {room.name}
            <small>
              {devices.filter((device) => device.roomName === room.name).length}
            </small>
          </button>
        ))}

        <div className="sidebar-bottom">
          <div className="connection-dot" />
          <div>
            <strong>Local system</strong>
            <p>{error ? "Connection issue" : "C++ API connected"}</p>
          </div>
        </div>
      </aside>

      <main className="main-content">
        <header className="topbar">
          <div className="breadcrumb">
            SmartHomeOS <span>/</span> Dashboard
          </div>

          <div className="topbar-right">
            <span className="preview-pill">
              <i />
              Live backend
            </span>
            <div className="avatar">T</div>
          </div>
        </header>

        <section className="welcome-row">
          <div>
            <p className="eyebrow">YOUR HOME AT A GLANCE</p>
            <h1>
              Good to see you<span>.</span>
            </h1>
            <p className="subtitle">
              Everything in your home, all in one place.
            </p>
          </div>

          <div className="date-card">
            <span>⌂</span>
            <div>
              <strong>My Smart Home</strong>
              <p>{rooms.length} rooms · Local control</p>
            </div>
          </div>
        </section>

        {error && (
          <div
            role="alert"
            style={{
              padding: "12px 16px",
              marginBottom: "16px",
              borderRadius: "10px",
              background: "#fff0f0",
              color: "#a12626",
            }}
          >
            {error}
            <button
              className="text-button"
              style={{ marginLeft: "12px" }}
              onClick={() => loadData(true)}
            >
              Retry
            </button>
          </div>
        )}

        <section className="stats-grid">
          <div className="stat-card">
            <div className="stat-top">
              <span>Total devices</span>
              <span className="stat-icon purple">▦</span>
            </div>
            <strong>{loading ? "—" : allDevices.length}</strong>
            <p>Across {rooms.length} rooms</p>
          </div>

          <div className="stat-card">
            <div className="stat-top">
              <span>Devices running</span>
              <span className="stat-icon green">◉</span>
            </div>
            <strong>
              {loading ? "—" : onCount}
              <small> / {loading ? "—" : allDevices.length}</small>
            </strong>
            <p className="positive">
              ● {loading ? "—" : allDevices.length - onCount} switched off
            </p>
          </div>

          <div className="stat-card">
            <div className="stat-top">
              <span>Active power</span>
              <span className="stat-icon amber">ϟ</span>
            </div>
            <strong>
              {loading ? "—" : (totalPower / 1000).toFixed(2)}
              <small> kW</small>
            </strong>
            <p>Based on enabled devices</p>
          </div>

          <div className="stat-card">
            <div className="stat-top">
              <span>Rooms</span>
              <span className="stat-icon blue">⌂</span>
            </div>
            <strong>{loading ? "—" : rooms.length}</strong>
            <p>Ready to manage</p>
          </div>
        </section>

        <section className="devices-section">
          <div className="section-heading">
            <div>
              <p className="eyebrow">CONTROL CENTER</p>
              <h2>
                {selectedRoom === "All Rooms"
                  ? "Your devices"
                  : selectedRoom}
              </h2>
              <p className="subtitle">
                Check status and control each appliance.
              </p>
            </div>

            <div className="bulk-actions">
              <button
                className="secondary-button"
                onClick={() => toggleAll(true)}
                disabled={loading || bulkBusy || allDevices.length === 0}
              >
                {bulkBusy ? "Updating..." : "Turn all on"}
              </button>

              <button
                className="secondary-button"
                onClick={() => toggleAll(false)}
                disabled={loading || bulkBusy || allDevices.length === 0}
              >
                {bulkBusy ? "Updating..." : "Turn all off"}
              </button>
            </div>
          </div>

          <div className="toolbar">
            <div className="search-box">
              <span>⌕</span>
              <input
                value={search}
                onChange={(event) => setSearch(event.target.value)}
                placeholder="Search devices..."
                aria-label="Search devices"
              />
            </div>

            <span className="result-count">
              {visibleRooms.reduce(
                (sum, room) => sum + room.devices.length,
                0
              )}{" "}
              devices shown
            </span>
          </div>

          {loading ? (
            <div className="empty-state">
              <h3>Loading your smart home...</h3>
              <p>Connecting to the C++ backend and SQLite.</p>
            </div>
          ) : visibleRooms.length === 0 ? (
            <div className="empty-state">
              <span>⌕</span>
              <h3>No devices found</h3>
              <p>
                Try a different search term or choose another room.
              </p>
            </div>
          ) : (
            visibleRooms.map((room) => (
              <div className="room-section" key={room.name}>
                <div className="room-heading">
                  <div className="room-title">
                    <span className="room-emoji">{room.icon}</span>
                    <h3>{room.name}</h3>
                    <span className="room-count">
                      {room.devices.length}
                    </span>
                  </div>

                  <button
                    className="text-button"
                    onClick={() => setSelectedRoom(room.name)}
                  >
                    View room →
                  </button>
                </div>

                <div className="device-grid">
                  {room.devices.map((device) => {
                    const busy =
                      busyDevices.includes(device.id) || bulkBusy;

                    return (
                      <article
                        className={`device-card ${
                          device.on ? "device-on" : ""
                        }`}
                        key={device.id}
                      >
                        <div className="device-card-top">
                          <div
                            className={`device-icon ${
                              device.on ? "icon-on" : ""
                            }`}
                          >
                            {typeIcons[device.type] || "🔌"}
                          </div>

                          <span
                            className={`status-tag ${
                              device.on ? "status-on" : "status-off"
                            }`}
                          >
                            <i /> {device.on ? "ON" : "OFF"}
                          </span>
                        </div>

                        <h4>{device.name}</h4>
                        <p className="device-type">
                          {device.type} · {device.power} W
                        </p>

                        <div className="device-card-bottom">
                          <span className="device-id">{device.id}</span>

                          <button
                            className={`toggle ${
                              device.on ? "toggle-on" : ""
                            }`}
                            onClick={() =>
                              toggleDevice(device.id, !device.on)
                            }
                            disabled={busy}
                            role="switch"
                            aria-checked={device.on}
                            aria-label={`Turn ${
                              device.on ? "off" : "on"
                            } ${device.name}`}
                          >
                            <span />
                          </button>
                        </div>

                        {busy && (
                          <p className="device-type">Updating...</p>
                        )}
                      </article>
                    );
                  })}
                </div>
              </div>
            ))
          )}
        </section>

        <footer className="footer">
          <span>SmartHomeOS · C++ core + SQLite</span>
          <span>Device controls connected to local API</span>
        </footer>
      </main>
    </div>
  );
}

export default App;