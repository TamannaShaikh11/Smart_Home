
import { useMemo, useState } from "react";
import "./App.css";

const initialRooms = [
  {
    name: "Living Room",
    icon: "🛋️",
    devices: [
      { id: "LIVING_LIGHT", name: "Main Light", type: "Light", power: 60, on: true },
      { id: "LIVING_FAN", name: "Ceiling Fan", type: "Fan", power: 75, on: true },
      { id: "LIVING_AC", name: "Air Conditioner", type: "AC", power: 1500, on: false },
      { id: "LIVING_LAMP", name: "Table Lamp", type: "Light", power: 40, on: false },
      { id: "LIVING_TV", name: "Television", type: "Other", power: 100, on: true },
    ],
  },
  {
    name: "Bedroom",
    icon: "🛏️",
    devices: [
      { id: "BED_LIGHT", name: "Bedroom Light", type: "Light", power: 60, on: false },
      { id: "BED_FAN", name: "Bedroom Fan", type: "Fan", power: 75, on: true },
      { id: "BED_AC", name: "Bedroom AC", type: "AC", power: 1500, on: false },
      { id: "BED_LAMP", name: "Bedside Lamp", type: "Light", power: 40, on: true },
      { id: "BED_HEATER", name: "Room Heater", type: "Heater", power: 2000, on: false },
    ],
  },
  {
    name: "Kitchen",
    icon: "🍳",
    devices: [
      { id: "KITCHEN_LIGHT", name: "Kitchen Light", type: "Light", power: 60, on: true },
      { id: "KITCHEN_FAN", name: "Exhaust Fan", type: "Fan", power: 50, on: false },
      { id: "KITCHEN_FRIDGE", name: "Refrigerator", type: "Other", power: 200, on: true },
    ],
  },
  {
    name: "Study Room",
    icon: "📚",
    devices: [
      { id: "STUDY_LIGHT", name: "Study Light", type: "Light", power: 60, on: true },
      { id: "STUDY_FAN", name: "Study Fan", type: "Fan", power: 75, on: false },
      { id: "STUDY_LAMP", name: "Desk Lamp", type: "Light", power: 25, on: true },
      { id: "STUDY_AC", name: "Study AC", type: "AC", power: 1200, on: false },
    ],
  },
  {
    name: "Bathroom",
    icon: "🚿",
    devices: [
      { id: "BATH_LIGHT", name: "Bathroom Light", type: "Light", power: 40, on: true },
      { id: "BATH_FAN", name: "Ventilation Fan", type: "Fan", power: 35, on: false },
      { id: "BATH_HEATER", name: "Water Heater", type: "Heater", power: 1500, on: false },
    ],
  },
];

const typeIcons = {
  Light: "💡",
  Fan: "🌀",
  AC: "❄️",
  Heater: "♨️",
  Other: "🔌",
};

function App() {
  const [rooms, setRooms] = useState(initialRooms);
  const [selectedRoom, setSelectedRoom] = useState("All Rooms");
  const [search, setSearch] = useState("");

  const allDevices = rooms.flatMap((room) =>
    room.devices.map((device) => ({ ...device, roomName: room.name }))
  );

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
        devices: room.devices.filter((device) =>
          `${device.name} ${device.type} ${device.id}`
            .toLowerCase()
            .includes(search.toLowerCase())
        ),
      }))
      .filter((room) => room.devices.length > 0);
  }, [rooms, selectedRoom, search]);

  function toggleDevice(deviceId) {
    setRooms((currentRooms) =>
      currentRooms.map((room) => ({
        ...room,
        devices: room.devices.map((device) =>
          device.id === deviceId ? { ...device, on: !device.on } : device
        ),
      }))
    );
  }

  function toggleAll(turnOn) {
    setRooms((currentRooms) =>
      currentRooms.map((room) => ({
        ...room,
        devices: room.devices.map((device) => ({ ...device, on: turnOn })),
      }))
    );
  }

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="brand">
          <div className="brand-icon">⌂</div>
          <div>
            <h2>SmartHome<span>OS</span></h2>
            <p>HOME CONTROL CENTER</p>
          </div>
        </div>

        <div className="nav-label">WORKSPACE</div>
        <button
          className={`nav-item ${selectedRoom === "All Rooms" ? "active" : ""}`}
          onClick={() => setSelectedRoom("All Rooms")}
        >
          <span>▦</span> Overview
        </button>

        <div className="nav-label room-label">YOUR ROOMS</div>
        {rooms.map((room) => (
          <button
            key={room.name}
            className={`nav-item ${selectedRoom === room.name ? "active" : ""}`}
            onClick={() => setSelectedRoom(room.name)}
          >
            <span>{room.icon}</span>
            {room.name}
            <small>{room.devices.length}</small>
          </button>
        ))}

        <div className="sidebar-bottom">
          <div className="connection-dot" />
          <div>
            <strong>Local system</strong>
            <p>UI preview mode</p>
          </div>
        </div>
      </aside>

      <main className="main-content">
        <header className="topbar">
          <div className="breadcrumb">SmartHomeOS <span>/</span> Dashboard</div>
          <div className="topbar-right">
            <span className="preview-pill"><i /> Demo data</span>
            <div className="avatar">T</div>
          </div>
        </header>

        <section className="welcome-row">
          <div>
            <p className="eyebrow">YOUR HOME AT A GLANCE</p>
            <h1>Good to see you<span>.</span></h1>
            <p className="subtitle">
              Everything in your home, all in one place.
            </p>
          </div>
          <div className="date-card">
            <span>⌂</span>
            <div>
              <strong>My Smart Home</strong>
              <p>5 rooms · Local control</p>
            </div>
          </div>
        </section>

        <section className="stats-grid">
          <div className="stat-card">
            <div className="stat-top">
              <span>Total devices</span><span className="stat-icon purple">▦</span>
            </div>
            <strong>{allDevices.length}</strong>
            <p>Across {rooms.length} rooms</p>
          </div>
          <div className="stat-card">
            <div className="stat-top">
              <span>Devices running</span><span className="stat-icon green">◉</span>
            </div>
            <strong>{onCount}<small> / {allDevices.length}</small></strong>
            <p className="positive">● {allDevices.length - onCount} switched off</p>
          </div>
          <div className="stat-card">
            <div className="stat-top">
              <span>Active power</span><span className="stat-icon amber">ϟ</span>
            </div>
            <strong>{(totalPower / 1000).toFixed(2)}<small> kW</small></strong>
            <p>Based on enabled devices</p>
          </div>
          <div className="stat-card">
            <div className="stat-top">
              <span>Rooms</span><span className="stat-icon blue">⌂</span>
            </div>
            <strong>{rooms.length}</strong>
            <p>Ready to manage</p>
          </div>
        </section>

        <section className="devices-section">
          <div className="section-heading">
            <div>
              <p className="eyebrow">CONTROL CENTER</p>
              <h2>{selectedRoom === "All Rooms" ? "Your devices" : selectedRoom}</h2>
              <p className="subtitle">Check status and control each appliance.</p>
            </div>
            <div className="bulk-actions">
              <button className="secondary-button" onClick={() => toggleAll(true)}>
                Turn all on
              </button>
              <button className="secondary-button" onClick={() => toggleAll(false)}>
                Turn all off
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
              {visibleRooms.reduce((sum, room) => sum + room.devices.length, 0)} devices shown
            </span>
          </div>

          {visibleRooms.length === 0 ? (
            <div className="empty-state">
              <span>⌕</span>
              <h3>No devices found</h3>
              <p>Try a different search term or choose another room.</p>
            </div>
          ) : (
            visibleRooms.map((room) => (
              <div className="room-section" key={room.name}>
                <div className="room-heading">
                  <div className="room-title">
                    <span className="room-emoji">{room.icon}</span>
                    <h3>{room.name}</h3>
                    <span className="room-count">{room.devices.length}</span>
                  </div>
                  <button
                    className="text-button"
                    onClick={() => setSelectedRoom(room.name)}
                  >
                    View room →
                  </button>
                </div>

                <div className="device-grid">
                  {room.devices.map((device) => (
                    <article
                      className={`device-card ${device.on ? "device-on" : ""}`}
                      key={device.id}
                    >
                      <div className="device-card-top">
                        <div className={`device-icon ${device.on ? "icon-on" : ""}`}>
                          {typeIcons[device.type] || "🔌"}
                        </div>
                        <span className={`status-tag ${device.on ? "status-on" : "status-off"}`}>
                          <i /> {device.on ? "ON" : "OFF"}
                        </span>
                      </div>

                      <h4>{device.name}</h4>
                      <p className="device-type">{device.type} · {device.power} W</p>

                      <div className="device-card-bottom">
                        <span className="device-id">{device.id}</span>
                        <button
                          className={`toggle ${device.on ? "toggle-on" : ""}`}
                          onClick={() => toggleDevice(device.id)}
                          role="switch"
                          aria-checked={device.on}
                          aria-label={`${device.on ? "Turn off" : "Turn on"} ${device.name}`}
                        >
                          <span />
                        </button>
                      </div>
                    </article>
                  ))}
                </div>
              </div>
            ))
          )}
        </section>

        <footer className="footer">
          <span>SmartHomeOS · C++ core + SQLite</span>
          <span>Frontend prototype · Controls use demo state only</span>
        </footer>
      </main>
    </div>
  );
}

export default App;