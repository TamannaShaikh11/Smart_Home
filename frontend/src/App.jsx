
import "./App.css";

import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";

const API_URL =
  import.meta.env.VITE_SMARTHOME_API_URL ?? "http://localhost:8080/api";
const FACE_SERVICE_URL =
  import.meta.env.VITE_SMARTHOME_FACE_URL ?? "http://127.0.0.1:8765";

const DEFAULT_TARIFF = {
  slab1: 3.96,
  slab2: 10.8,
  slab3: 15.03,
  slab4: 17.53,
  wheeling: 1.6,
  fixedCharge: 100,
  dutyPercent: 16,
};

const EMPTY_PREFERENCES = {
  acTemperature: -1,
  heaterTemperature: -1,
  fanSpeed: -1,
  lightOn: -1,
  automaticApplyEnabled: false,
  firstApplicationConfirmed: false,
};

function getRoomLabel(room) {
  return typeof room === "string" ? room : room?.name ?? room?.roomName ?? "";
}

function createProfileId() {
  return `user_${Date.now()}`;
}

async function getApiError(response) {
  try {
    const data = await response.json();
    return data.error || data.message || `Request failed (${response.status})`;
  } catch {
    return `Request failed (${response.status})`;
  }
}

async function fetchFaceJson(path, options = {}) {
  const response = await fetch(`${FACE_SERVICE_URL}${path}`, options);
  let data = {};
  try {
    data = await response.json();
  } catch {
    if (response.ok) throw new Error("Local face service returned invalid JSON.");
  }
  if (!response.ok)
    throw new Error(data.error || `Local face service request failed (${response.status})`);
  return data;
}

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

function formatModeActionValue(action, value) {
  if (action.property === "policy") return String(value);
  if (action.property === "power" && typeof value === "boolean")
    return value ? "ON" : "OFF";
  if (action.property === "brightness") return `${value}%`;
  if (action.property === "colorTemperature") return `${value} K`;
  if (action.property === "setting") {
    if (action.deviceType === "Fan") return `Speed ${value}`;
    if (["AC", "Heater"].includes(action.deviceType)) return `${value}°C`;
  }
  return String(value ?? "unknown");
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
  const [energySummary, setEnergySummary] = useState(null);
  const [energySummaryLoading, setEnergySummaryLoading] = useState(true);
  const [energySummaryError, setEnergySummaryError] = useState("");
  const [nowSeconds, setNowSeconds] = useState(0);
  const [error, setError] = useState("");
  const [energyError, setEnergyError] = useState("");

  const [search, setSearch] = useState("");
  const [selectedRoom, setSelectedRoom] = useState("All Rooms");
  const [updatingDevices, setUpdatingDevices] = useState({});
  const [deviceSettingBusy, setDeviceSettingBusy] = useState({});
  const [deviceSettingErrors, setDeviceSettingErrors] = useState({});
  const [deviceSettingDrafts, setDeviceSettingDrafts] = useState({});
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
    initialOn: false,
    temporary: true,
    lifetimeValue: "30",
    lifetimeUnit: "minutes",
    setting: "24",
    lightType: "standard",
    brightness: "100",
    color: "#FFFFFF",
    colorTemperature: "4000",
  });
  const [editingDevice, setEditingDevice] = useState(null);
  const [editingDeviceName, setEditingDeviceName] = useState("");
  const [editingDevicePower, setEditingDevicePower] = useState("");
  const deviceRefreshVersion = useRef(0);

  const [tariff, setTariff] = useState(DEFAULT_TARIFF);

  const [tariffInputs, setTariffInputs] = useState({
    ...DEFAULT_TARIFF,
  });

  const [showTariffSettings, setShowTariffSettings] = useState(false);
  const [activeSection, setActiveSection] = useState("overview");
  const [seasonPresets, setSeasonPresets] = useState([]);
  const [moodPresets, setMoodPresets] = useState([]);
  const [modeHistory, setModeHistory] = useState([]);
  const [selectedSeasonId, setSelectedSeasonId] = useState("");
  const [selectedMoodId, setSelectedMoodId] = useState("");
  const [modeTargetRooms, setModeTargetRooms] = useState([]);
  const [modePreview, setModePreview] = useState(null);
  const [modeApplicationResult, setModeApplicationResult] = useState(null);
  const [modePreviewBusy, setModePreviewBusy] = useState(false);
  const [modeApplyBusy, setModeApplyBusy] = useState(false);
  const [modeConfirming, setModeConfirming] = useState(false);
  const [modeRestoring, setModeRestoring] = useState(false);
  const [modeError, setModeError] = useState("");
  const [modeMessage, setModeMessage] = useState("");
  const [modePresetType, setModePresetType] = useState("season");
  const [modePresetId, setModePresetId] = useState("");
  const [modeRuleDeviceId, setModeRuleDeviceId] = useState("");
  const [modeRuleProperty, setModeRuleProperty] = useState("");
  const [modeRuleValue, setModeRuleValue] = useState("");
  const [modePresetBusy, setModePresetBusy] = useState(false);

  const fetchJson = useCallback(async (url, options = {}) => {
    const response = await fetch(url, options);

    if (!response.ok) {
      throw new Error(await getApiError(response));
    }

    return response.json();
  }, []);

  const stopCamera = async (finalStatus = "disabled") => {
    const session = cameraSessionRef.current;
    cameraRequestId.current += 1;
    cameraStreamRef.current?.getTracks().forEach((track) => track.stop());
    cameraStreamRef.current = null;
    cameraSessionRef.current = null;
    setCameraStream(null);
    setCameraSession(null);
    setCameraStarting(false);
    setCameraStatus(finalStatus);
    if (finalStatus === "disabled") setCameraError("");
    if (session) {
      const cleanupErrors = [];
      try {
        await fetchFaceJson(
          `/recognition/sessions/${encodeURIComponent(session.sessionId)}`,
          { method: "DELETE" }
        );
      } catch (err) {
        if (!err.message.includes("(404)")) cleanupErrors.push(err.message);
      }
      try {
        await fetchJson(
          `${API_URL}/automation/sessions/${encodeURIComponent(session.sessionId)}`,
          { method: "DELETE" }
        );
      } catch (err) {
        if (!err.message.includes("(404)")) cleanupErrors.push(err.message);
      }
      if (cleanupErrors.length > 0)
        setCameraError(`Camera stopped locally, but session cleanup reported: ${cleanupErrors.join("; ")}`);
    }
  };

  useEffect(() => {
    stopCameraRef.current = stopCamera;
  });

  const startCamera = async () => {
    if (!selectedRoom || selectedRoom === "All Rooms") {
      setCameraStatus("error");
      setCameraError("Select one room before starting recognition.");
      return;
    }
    if (roomCameraSettingsRoom !== selectedRoom) {
      setCameraStatus("error");
      setCameraError("Wait for this room's camera settings to load before starting recognition.");
      return;
    }
    if (!automationEnabled) {
      setCameraStatus("error");
      setCameraError("Enable camera automation before starting recognition.");
      return;
    }
    if (!roomCameraEnabled) {
      setCameraStatus("error");
      setCameraError("Enable camera monitoring for this room before starting recognition.");
      return;
    }
    const requestId = cameraRequestId.current + 1;
    cameraRequestId.current = requestId;
    setCameraStarting(true);
    setCameraError("");

    if (!navigator.mediaDevices?.getUserMedia) {
      setCameraStatus("unavailable");
      setCameraError("This browser does not provide local camera access.");
      setCameraStarting(false);
      return;
    }

    try {
      await fetchFaceJson("/health");
      const stream = await navigator.mediaDevices.getUserMedia({
        video: true,
        audio: false,
      });
      cameraStreamRef.current = stream;
      if (cameraRequestId.current !== requestId) {
        stream.getTracks().forEach((track) => track.stop());
        cameraStreamRef.current = null;
        return;
      }
      const session = await fetchJson(`${API_URL}/automation/sessions`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ roomName: selectedRoom }),
      });
      cameraSessionRef.current = session;
      await fetchFaceJson("/recognition/sessions", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          sessionId: session.sessionId,
          roomName: selectedRoom,
        }),
      });
      setCameraSession(session);
      setCameraStream(stream);
      setCameraStatus("active");
      setRecognition({
        status: "pending",
        displayName: "",
        enrollmentId: "",
      });
      setAutomationPreview(null);
      setAutomationApplyResult(null);
      handledEnrollmentRef.current = "";
    } catch (err) {
      if (cameraRequestId.current !== requestId) return;
      cameraStreamRef.current?.getTracks().forEach((track) => track.stop());
      cameraStreamRef.current = null;
      setCameraStream(null);
      const session = cameraSessionRef.current;
      cameraSessionRef.current = null;
      if (session?.sessionId) {
        try {
          await fetchFaceJson(
            `/recognition/sessions/${encodeURIComponent(session.sessionId)}`,
            { method: "DELETE" }
          );
        } catch {
          // The service may have rejected startup before creating a session.
        }
        try {
          await fetchJson(
            `${API_URL}/automation/sessions/${encodeURIComponent(session.sessionId)}`,
            { method: "DELETE" }
          );
        } catch {
          // The API may have already invalidated this session.
        }
      }
      if (err.name === "NotAllowedError" || err.name === "SecurityError") {
        setCameraStatus("denied");
        setCameraError("Camera permission was denied. Change the localhost site permission, then retry.");
      } else if (err.name === "NotFoundError" || err.name === "DevicesNotFoundError") {
        setCameraStatus("unavailable");
        setCameraError("Camera Offline: no camera device is available to this browser.");
      } else if (String(err.message).includes("Failed to fetch")) {
        setCameraStatus("unavailable");
        setCameraError("Camera Automation service is offline. Manual Control remains available.");
      } else {
        setCameraStatus("error");
        setCameraError(`Could not start recognition: ${err.message}`);
      }
    } finally {
      if (cameraRequestId.current === requestId) setCameraStarting(false);
    }
  };

  const askAssistant = async (submittedQuestion) => {
    const question = submittedQuestion.trim();
    if (!question || question.length > 1000 || assistantBusy) return;

    setAssistantBusy(true);
    setAssistantError("");
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 20000);
    try {
      const result = await fetchJson(`${API_URL}/assistant/chat`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ question }),
        signal: controller.signal,
      });
      setAssistantMessages((messages) => [
        ...messages,
        { question, ...result },
      ]);
      setAssistantQuestion("");
    } catch (err) {
      setAssistantError(
        err.name === "AbortError"
          ? "The assistant request timed out. Retry when the backend or provider is available."
          : `Could not get an energy answer: ${err.message}`
      );
    } finally {
      clearTimeout(timeout);
      setAssistantBusy(false);
    }
  };

  const sendAssistantQuestion = (event) => {
    event.preventDefault();
    askAssistant(assistantQuestion);
  };

  const [profiles, setProfiles] = useState([]);
  const [profilesLoading, setProfilesLoading] = useState(false);
  const [selectedProfileId, setSelectedProfileId] = useState("");
  const [profileNameInput, setProfileNameInput] = useState("");
  const [profileBusy, setProfileBusy] = useState(false);
  const [profileMessage, setProfileMessage] = useState("");

  const [preferenceRoom, setPreferenceRoom] = useState("");
  const [preferences, setPreferences] = useState(EMPTY_PREFERENCES);
  const [preferencesForKey, setPreferencesForKey] = useState("");
  const [preferenceErrorForKey, setPreferenceErrorForKey] = useState("");
  const [preferencesLoading, setPreferencesLoading] = useState(false);
  const [preferencesSaved, setPreferencesSaved] = useState(false);
  const [preferenceMessage, setPreferenceMessage] = useState("");
  const [preferenceError, setPreferenceError] = useState("");
  const [applyBusy, setApplyBusy] = useState(false);
  const [applyResult, setApplyResult] = useState(null);
  const [assistantQuestion, setAssistantQuestion] = useState("");
  const [assistantMessages, setAssistantMessages] = useState([]);
  const [assistantBusy, setAssistantBusy] = useState(false);
  const [assistantError, setAssistantError] = useState("");
  const [roomOperatingMode, setRoomOperatingMode] = useState("manual");
  const [automationEnabled, setAutomationEnabled] = useState(false);
  const [roomCameraEnabled, setRoomCameraEnabled] = useState(false);
  const [roomCameraSettingsRoom, setRoomCameraSettingsRoom] = useState("");
  const [roomCameraSettingsBusy, setRoomCameraSettingsBusy] = useState(false);
  const [automationStatus, setAutomationStatus] = useState("checking");
  const [automationError, setAutomationError] = useState("");
  const [automationBusy, setAutomationBusy] = useState(false);
  const [automationSession, setCameraSession] = useState(null);
  const [recognition, setRecognition] = useState({
    status: "waiting",
    displayName: "",
    enrollmentId: "",
  });
  const [automationPreview, setAutomationPreview] = useState(null);
  const [automationApplyResult, setAutomationApplyResult] = useState(null);
  const [automationEvents, setAutomationEvents] = useState([]);
  const [enrollments, setEnrollments] = useState([]);
  const [enrollmentProfileId, setEnrollmentProfileId] = useState("");
  const [enrollmentConsent, setEnrollmentConsent] = useState(false);
  const [enrollmentBusy, setEnrollmentBusy] = useState(false);
  const [enrollmentMessage, setEnrollmentMessage] = useState("");
  const [deletingEnrollment, setDeletingEnrollment] = useState("");
  const [cameraStatus, setCameraStatus] = useState("disabled");
  const [cameraStream, setCameraStream] = useState(null);
  const [cameraStarting, setCameraStarting] = useState(false);
  const setCameraError = useCallback(
    (message) => setAutomationError(message),
    []
  );
  const cameraVideoRef = useRef(null);
  const cameraStreamRef = useRef(null);
  const startCameraRef = useRef(null);
  startCameraRef.current = startCamera;
  const cameraSessionRef = useRef(null);
  const stopCameraRef = useRef(null);
  const cameraRequestId = useRef(0);
  const recognitionIntervalRef = useRef(null);
  const handledEnrollmentRef = useRef("");
  const automaticApplyAttemptRef = useRef("");
  const manualCameraOverrideRef = useRef(false);
  const cameraAutoStartAttemptRef = useRef("");

  useEffect(() => {
    const video = cameraVideoRef.current;
    if (video) video.srcObject = cameraStream;
    return () => {
      if (video) video.srcObject = null;
    };
  }, [cameraStream, activeSection, roomOperatingMode]);

  useEffect(() => () => {
    const session = cameraSessionRef.current;
    cameraRequestId.current += 1;
    cameraStreamRef.current?.getTracks().forEach((track) => track.stop());
    cameraStreamRef.current = null;
    if (session?.sessionId) {
      fetch(`${FACE_SERVICE_URL}/recognition/sessions/${encodeURIComponent(session.sessionId)}`, {
        method: "DELETE",
        keepalive: true,
      }).catch(() => {});
      fetch(`${API_URL}/automation/sessions/${encodeURIComponent(session.sessionId)}`, {
        method: "DELETE",
        keepalive: true,
      }).catch(() => {});
    }
  }, []);

  useEffect(() => {
    let active = true;
    const loadAutomationState = async () => {
      try {
        const [status, enrollmentData, eventData] = await Promise.all([
          fetchJson(`${API_URL}/automation/status`),
          fetchJson(`${API_URL}/automation/enrollments`),
          fetchJson(`${API_URL}/automation/events?limit=5`),
        ]);
        if (!active) return;
        setAutomationEnabled(Boolean(status.enabled));
        setEnrollments(Array.isArray(enrollmentData) ? enrollmentData : []);
        setAutomationEvents(Array.isArray(eventData) ? eventData : []);
        try {
          await fetchFaceJson("/health");
          setAutomationStatus("online");
        } catch {
          setAutomationStatus("offline");
        }
      } catch (err) {
        if (!active) return;
        try {
          await fetchFaceJson("/health");
          if (active) setAutomationStatus("online");
        } catch {
          if (active) setAutomationStatus("offline");
        }
        setAutomationError(`Could not load automation settings: ${err.message}`);
      }
    };
    loadAutomationState();
    return () => {
      active = false;
    };
  }, [fetchJson]);

  useEffect(() => {
    let active = true;
    const checkFaceService = async () => {
      try {
        await fetchFaceJson("/health");
        if (active) setAutomationStatus("online");
      } catch {
        if (active) setAutomationStatus("offline");
      }
    };
    void checkFaceService();
    const interval = window.setInterval(checkFaceService, 5000);
    return () => {
      active = false;
      window.clearInterval(interval);
    };
  }, []);

  useEffect(() => {
    let active = true;
    setRoomCameraEnabled(false);
    setRoomCameraSettingsRoom("");
    if (!selectedRoom || selectedRoom === "All Rooms") {
      setRoomCameraSettingsBusy(false);
      return () => { active = false; };
    }
    setRoomCameraSettingsBusy(true);
    fetchJson(
      `${API_URL}/automation/room-settings?roomName=${encodeURIComponent(selectedRoom)}`
    )
      .then((settings) => {
        if (active) {
          setRoomCameraEnabled(Boolean(settings.cameraEnabled));
          setRoomCameraSettingsRoom(selectedRoom);
        }
      })
      .catch((err) => {
        if (active)
          setAutomationError(
            `Could not load ${selectedRoom} camera settings: ${err.message}`
          );
      })
      .finally(() => {
        if (active) setRoomCameraSettingsBusy(false);
      });
    return () => { active = false; };
  }, [selectedRoom, fetchJson]);

  useEffect(() => {
    if (activeSection !== "devices" ||
        !selectedRoom || selectedRoom === "All Rooms" ||
        roomCameraSettingsRoom !== selectedRoom ||
        !roomCameraEnabled || !automationEnabled ||
        automationStatus !== "online" || manualCameraOverrideRef.current ||
        cameraSessionRef.current || cameraStarting ||
        cameraAutoStartAttemptRef.current === selectedRoom)
      return;
    cameraAutoStartAttemptRef.current = selectedRoom;
    setRoomOperatingMode("camera");
    void startCameraRef.current?.();
  }, [
    activeSection,
    automationEnabled,
    automationStatus,
    cameraStarting,
    roomCameraEnabled,
    roomCameraSettingsRoom,
    selectedRoom,
  ]);

  const loadProfiles = useCallback(async () => {
    setProfilesLoading(true);
    try {
      const data = await fetchJson(`${API_URL}/profiles`);
      const list = Array.isArray(data) ? data : [];

      setProfiles(list);
      setProfileMessage("");
      return true;
    } catch (err) {
      setProfileMessage(`Could not load profiles: ${err.message}`);
      return false;
    } finally {
      setProfilesLoading(false);
    }
  }, [fetchJson]);

  const refreshAutomationMetadata = async () => {
    const [enrollmentData, eventData] = await Promise.all([
      fetchJson(`${API_URL}/automation/enrollments`),
      fetchJson(`${API_URL}/automation/events?limit=5`),
    ]);
    setEnrollments(Array.isArray(enrollmentData) ? enrollmentData : []);
    setAutomationEvents(Array.isArray(eventData) ? eventData : []);
  };

  const updateAutomationEnabled = async (enabled) => {
    setAutomationBusy(true);
    setAutomationError("");
    try {
      if (!enabled && cameraSessionRef.current)
        await stopCamera();
      const result = await fetchJson(`${API_URL}/automation/settings`, {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ enabled }),
      });
      setAutomationEnabled(result.enabled);
      if (enabled) cameraAutoStartAttemptRef.current = "";
      try {
        await fetchFaceJson("/health");
        setAutomationStatus("online");
      } catch {
        setAutomationStatus("offline");
      }
      if (!enabled) {
        setRecognition({ status: "waiting", displayName: "", enrollmentId: "" });
        setAutomationPreview(null);
      }
    } catch (err) {
      setAutomationError(`Could not update automation setting: ${err.message}`);
    } finally {
      setAutomationBusy(false);
    }
  };

  const updateRoomCameraEnabled = async (enabled) => {
    if (!selectedRoom || selectedRoom === "All Rooms") return;
    setRoomCameraSettingsBusy(true);
    setAutomationError("");
    try {
      if (!enabled && cameraSessionRef.current)
        await stopCamera();
      const settings = await fetchJson(`${API_URL}/automation/room-settings`, {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ roomName: selectedRoom, cameraEnabled: enabled }),
      });
      setRoomCameraEnabled(Boolean(settings.cameraEnabled));
      setRoomCameraSettingsRoom(selectedRoom);
      if (enabled) cameraAutoStartAttemptRef.current = "";
    } catch (err) {
      setAutomationError(
        `Could not update ${selectedRoom} camera setting: ${err.message}`
      );
    } finally {
      setRoomCameraSettingsBusy(false);
    }
  };

  const enrollHouseholdMember = async (event) => {
    event.preventDefault();
    const profile = profiles.find((item) => item.id === enrollmentProfileId);
    if (!profile || !enrollmentConsent || enrollmentBusy || cameraSessionRef.current)
      return;
    if (!navigator.mediaDevices?.getUserMedia) {
      setEnrollmentMessage("This browser does not support local camera access.");
      return;
    }

    setEnrollmentBusy(true);
    setEnrollmentMessage("Requesting camera access for local enrollment…");
    let stream;
    let createdEnrollmentId = "";
    try {
      await fetchFaceJson("/health");
      stream = await navigator.mediaDevices.getUserMedia({
        video: true,
        audio: false,
      });
      cameraStreamRef.current = stream;
      setCameraStream(stream);
      setCameraStatus("enrolling");

      const video = cameraVideoRef.current;
      if (!video) throw new Error("The local enrollment preview is unavailable.");
      video.srcObject = stream;
      await video.play();
      const readyDeadline = Date.now() + 6000;
      while (video.readyState < 2 && Date.now() < readyDeadline)
        await new Promise((resolve) => setTimeout(resolve, 100));
      if (video.readyState < 2)
        throw new Error("Camera did not provide a usable local video frame.");

      setEnrollmentMessage("Capturing 10 local samples. Keep one consenting person's face centered.");
      const canvas = document.createElement("canvas");
      const context = canvas.getContext("2d", { willReadFrequently: true });
      if (!context) throw new Error("Could not create a local enrollment frame.");
      const scale = Math.min(1, 640 / video.videoWidth);
      canvas.width = Math.round(video.videoWidth * scale);
      canvas.height = Math.round(video.videoHeight * scale);
      const frames = [];
      for (let index = 0; index < 10; index += 1) {
        context.drawImage(video, 0, 0, canvas.width, canvas.height);
        frames.push(canvas.toDataURL("image/jpeg", 0.72));
        await new Promise((resolve) => setTimeout(resolve, 350));
      }

      const enrollment = await fetchJson(`${API_URL}/automation/enrollments`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ profileId: profile.id, consent: true }),
      });
      createdEnrollmentId = enrollment.id;
      await fetchFaceJson(
        `/enrollments/${encodeURIComponent(enrollment.id)}/train`,
        {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            displayName: enrollment.profileName,
            frames,
          }),
        }
      );
      await refreshAutomationMetadata();
      setEnrollmentMessage(`Local enrollment linked to ${profile.displayName}. Face samples are encrypted on this Windows account.`);
      setEnrollmentConsent(false);
    } catch (err) {
      if (createdEnrollmentId) {
        try {
          await fetchJson(
            `${API_URL}/automation/enrollments/${encodeURIComponent(createdEnrollmentId)}`,
            { method: "DELETE" }
          );
        } catch (cleanupError) {
          setEnrollmentMessage(
            `Enrollment failed: ${err.message}. The profile link cleanup also failed: ${cleanupError.message}`
          );
        }
      }
      if (!createdEnrollmentId || !err.message.includes("cleanup also failed"))
        setEnrollmentMessage(`Enrollment failed: ${err.message}`);
    } finally {
      stream?.getTracks().forEach((track) => track.stop());
      cameraStreamRef.current = null;
      setCameraStream(null);
      setCameraStatus("disabled");
      setEnrollmentBusy(false);
    }
  };

  const removeEnrollment = async (enrollment) => {
    if (!window.confirm(`Delete the local face enrollment linked to ${enrollment.profileName}?`))
      return;
    setDeletingEnrollment(enrollment.id);
    setEnrollmentMessage("");
    try {
      try {
        await fetchFaceJson(
          `/enrollments/${encodeURIComponent(enrollment.id)}`,
          { method: "DELETE" }
        );
      } catch (err) {
        if (!err.message.includes("(404)")) throw err;
      }
      await fetchJson(
        `${API_URL}/automation/enrollments/${encodeURIComponent(enrollment.id)}`,
        { method: "DELETE" }
      );
      await refreshAutomationMetadata();
      setEnrollmentMessage(`Enrollment for ${enrollment.profileName} was deleted.`);
    } catch (err) {
      setEnrollmentMessage(`Could not delete enrollment: ${err.message}`);
    } finally {
      setDeletingEnrollment("");
    }
  };

  const applyAutomationPreview = async (
    plan = automationPreview,
    session = automationSession,
    automatic = false
  ) => {
    if (!session || !plan ||
        recognition.status !== "matched" ||
        recognition.enrollmentId !== plan.enrollmentId ||
        (automatic &&
          (!plan.automaticApplyEnabled || !plan.firstApplicationConfirmed)) ||
        automationApplyResult?.success === true)
      return;
    setAutomationBusy(true);
    setAutomationError("");
    try {
      const result = await fetchJson(
        `${API_URL}/profiles/${encodeURIComponent(plan.profileId)}/preferences/apply`,
        {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            roomName: session.roomName,
            automationSessionId: session.sessionId,
            confirmed: true,
            automatic,
          }),
        }
      );
      setAutomationApplyResult(result);
      await Promise.all([
        refreshAutomationMetadata(),
        loadDashboard(false),
        loadEnergySessions(),
        loadEnergySummary(),
      ]);
    } catch (err) {
      setAutomationError(
        `Could not ${automatic ? "automatically apply" : "apply the confirmed"} room preferences: ${err.message}`
      );
    } finally {
      setAutomationBusy(false);
    }
  };

  const applyAutomationPreviewRef = useRef(null);
  applyAutomationPreviewRef.current = applyAutomationPreview;

  useEffect(() => {
    if (!automationSession || !cameraStream ||
        recognition.status !== "matched" ||
        recognition.enrollmentId !== automationPreview?.enrollmentId ||
        !automationPreview?.automaticApplyEnabled ||
        !automationPreview?.firstApplicationConfirmed)
      return;
    const key = `${automationSession.sessionId}:${automationPreview.enrollmentId}`;
    if (automaticApplyAttemptRef.current === key) return;
    automaticApplyAttemptRef.current = key;
    void applyAutomationPreviewRef.current?.(
      automationPreview,
      automationSession,
      true
    );
  }, [
    automationPreview,
    automationSession,
    cameraStream,
    recognition.enrollmentId,
    recognition.status,
  ]);

  useEffect(() => {
    if (!automationSession || !cameraStream || roomOperatingMode !== "camera")
      return undefined;
    let cancelled = false;
    let processing = false;
    const session = automationSession;

    const recognizeFrame = async () => {
      if (cancelled || processing) return;
      const video = cameraVideoRef.current;
      if (!video || video.readyState < 2 || video.videoWidth < 1) return;
      processing = true;
      try {
        const backendStatus = await fetchJson(
          `${API_URL}/automation/sessions/${encodeURIComponent(session.sessionId)}`
        );
        if (!backendStatus.enabled || backendStatus.roomName !== session.roomName) {
          setAutomationEnabled(Boolean(backendStatus.enabled));
          throw new Error("The room recognition session was disabled or changed.");
        }
        const canvas = document.createElement("canvas");
        const scale = Math.min(1, 640 / video.videoWidth);
        canvas.width = Math.round(video.videoWidth * scale);
        canvas.height = Math.round(video.videoHeight * scale);
        const context = canvas.getContext("2d", { willReadFrequently: true });
        if (!context) throw new Error("Could not prepare a local recognition frame.");
        context.drawImage(video, 0, 0, canvas.width, canvas.height);
        const result = await fetchFaceJson(
          `/recognition/sessions/${encodeURIComponent(session.sessionId)}/frame`,
          {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
              roomName: session.roomName,
              frame: canvas.toDataURL("image/jpeg", 0.68),
            }),
          }
        );
        if (cancelled) return;
        setRecognition((current) => ({
          ...current,
          ...result,
          enrollmentId: result.enrollmentId ?? "",
        }));
        await fetchJson(
          `${API_URL}/automation/sessions/${encodeURIComponent(session.sessionId)}/observation`,
          {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
              roomName: session.roomName,
              status: result.status,
              ...(result.status === "matched"
                ? { enrollmentId: result.enrollmentId }
                : {}),
            }),
          }
        );
        if (result.status === "matched" && result.enrollmentId) {
          const key = `${session.sessionId}:${result.enrollmentId}`;
          if (handledEnrollmentRef.current !== key) {
            handledEnrollmentRef.current = key;
            setAutomationApplyResult(null);
            try {
              const preview = await fetchJson(
                `${API_URL}/automation/sessions/${encodeURIComponent(session.sessionId)}/resolve`,
                {
                  method: "POST",
                  headers: { "Content-Type": "application/json" },
                  body: JSON.stringify({
                    roomName: session.roomName,
                    enrollmentId: result.enrollmentId,
                  }),
                }
              );
              if (!cancelled) setAutomationPreview(preview);
            } catch (err) {
              if (!cancelled) {
                setAutomationPreview(null);
                setAutomationError(
                  err.message.includes("No saved preferences")
                    ? `${result.displayName} is recognized, but has no saved preferences for ${session.roomName}. Manual Control remains available.`
                    : `Could not load the matched profile preferences: ${err.message}`
                );
              }
            }
          }
        } else {
          handledEnrollmentRef.current = "";
          setAutomationPreview(null);
          setAutomationApplyResult(null);
          if (result.status === "unknown")
            setAutomationError("");
        }
        setCameraStatus("active");
      } catch (err) {
        if (cancelled) return;
        setCameraStatus("unavailable");
        setAutomationStatus("offline");
        setRecognition({ status: "failed", displayName: "", enrollmentId: "" });
        setAutomationError(
          `Camera Offline or automation service unavailable: ${err.message}. No occupancy is inferred.`
        );
        void stopCameraRef.current?.("unavailable");
      } finally {
        processing = false;
      }
    };

    recognizeFrame();
    recognitionIntervalRef.current = window.setInterval(recognizeFrame, 1100);
    return () => {
      cancelled = true;
      window.clearInterval(recognitionIntervalRef.current);
      recognitionIntervalRef.current = null;
    };
  }, [
    automationSession,
    cameraStream,
    fetchJson,
    roomOperatingMode,
  ]);

  const changeSelectedRoom = async (roomName) => {
    if (cameraSessionRef.current || cameraStarting)
      await stopCamera();
    manualCameraOverrideRef.current = false;
    cameraAutoStartAttemptRef.current = "";
    setSelectedRoom(roomName);
    setRoomOperatingMode("manual");
    setAutomationPreview(null);
    setAutomationApplyResult(null);
  };

  const switchToManualControl = async () => {
    manualCameraOverrideRef.current = true;
    if (cameraSessionRef.current)
      await stopCamera();
    setRoomOperatingMode("manual");
  };

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
      const id = createProfileId();

      await fetchJson(`${API_URL}/profiles`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ id, displayName }),
      });

      setProfileNameInput("");
      const refreshed = await loadProfiles();
      setSelectedProfileId(id);
      setPreferencesLoading(true);
      setProfileMessage(
        refreshed
          ? `Profile "${displayName}" created successfully.`
          : `Profile "${displayName}" was created, but the profile list could not be refreshed.`
      );
    } catch (err) {
      setProfileMessage(`Could not create profile: ${err.message}`);
    } finally {
      setProfileBusy(false);
    }
  };

  const loadDashboard = useCallback(async (showLoading = false) => {
    if (showLoading) setLoading(true);
    const refreshVersion = deviceRefreshVersion.current;

    try {
      const [roomsData, devicesData] = await Promise.all([
        fetchJson(`${API_URL}/rooms`),
        fetchJson(`${API_URL}/devices`),
      ]);

      setRooms(Array.isArray(roomsData) ? roomsData : []);
      if (refreshVersion === deviceRefreshVersion.current)
        setDevices(Array.isArray(devicesData) ? devicesData : []);
      setError("");
    } catch (err) {
      setError(
        `Unable to load SmartHomeOS data. Check that SmartHomeAPI.exe is running. ${err.message}`
      );
    } finally {
      setLoading(false);
    }
  }, [fetchJson]);

  const loadModeData = useCallback(async () => {
    try {
      const [seasons, moods, history] = await Promise.all([
        fetchJson(`${API_URL}/modes/seasons`),
        fetchJson(`${API_URL}/modes/moods`),
        fetchJson(`${API_URL}/modes/history?limit=50`),
      ]);
      setSeasonPresets(Array.isArray(seasons) ? seasons : []);
      setMoodPresets(Array.isArray(moods) ? moods : []);
      setModeHistory(Array.isArray(history) ? history : []);
      setModeError("");
    } catch (err) {
      setModeError(`Could not load Season & Mood Modes: ${err.message}`);
    }
  }, [fetchJson]);

  useEffect(() => {
    if (activeSection === "modes") void loadModeData();
  }, [activeSection, loadModeData]);

  useEffect(() => {
    const availableRooms = rooms.map(getRoomLabel).filter(Boolean);
    if (availableRooms.length === 0) {
      if (modeTargetRooms.length > 0) setModeTargetRooms([]);
      return;
    }
    if (modeTargetRooms.length > 1) {
      setModeTargetRooms(modeTargetRooms.slice(0, 1));
    } else if (
      modeTargetRooms.length === 1 &&
      !availableRooms.includes(modeTargetRooms[0])
    ) {
      setModeTargetRooms([]);
    }
  }, [rooms, modeTargetRooms]);

  const buildModeSelection = () => ({
    seasonId: selectedSeasonId,
    moodId: selectedMoodId,
    roomNames: modeTargetRooms,
    deviceIds: [],
  });

  const generateModePreview = async () => {
    setModePreviewBusy(true);
    setModeError("");
    setModeMessage("");
    setModeApplicationResult(null);
    setModeConfirming(false);
    try {
      const preview = await fetchJson(`${API_URL}/modes/preview`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(buildModeSelection()),
      });
      setModePreview(preview);
    } catch (err) {
      setModePreview(null);
      setModeError(`Could not generate mode preview: ${err.message}`);
    } finally {
      setModePreviewBusy(false);
    }
  };

  const cancelModePreview = () => {
    setModePreview(null);
    setModeConfirming(false);
    setModeMessage("Preview cancelled. No device settings were changed.");
  };

  const confirmModeApplication = async () => {
    if (!modePreview?.previewId) return;
    setModeApplyBusy(true);
    setModeError("");
    try {
      const result = await fetchJson(`${API_URL}/modes/apply`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          previewId: modePreview.previewId,
          confirmed: true,
        }),
      });
      setModePreview(null);
      setModeConfirming(false);
      setModeMessage(
        result.outcome === "applied"
          ? `Mode applied successfully (${result.summary?.applied ?? 0} setting changes).`
          : result.outcome === "partial"
            ? `Mode applied partially: ${result.summary?.applied ?? 0} changed, ${result.summary?.skipped ?? 0} skipped, ${result.summary?.failed ?? 0} failed. Review each result below.`
            : result.message ?? "No device changes were needed."
      );
      setModeApplicationResult(result);
      await Promise.all([loadDashboard(false), loadModeData()]);
    } catch (err) {
      setModeError(`Could not apply the confirmed mode: ${err.message}`);
      setModeConfirming(false);
    } finally {
      setModeApplyBusy(false);
    }
  };

  const restorePreviousMode = async () => {
    setModeRestoring(true);
    setModeError("");
    try {
      const result = await fetchJson(`${API_URL}/modes/restore`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          confirmed: true,
          roomName: modeTargetRooms[0],
        }),
      });
      setModeMessage(
        result.success
          ? "The previous device settings were restored."
          : "Restore was partial. Review the per-device result in history."
      );
      await Promise.all([loadDashboard(false), loadModeData()]);
    } catch (err) {
      setModeError(`Could not restore the previous mode state: ${err.message}`);
    } finally {
      setModeRestoring(false);
      setModeConfirming(false);
    }
  };

  const modePresetOptions =
    modePresetType === "season" ? seasonPresets : moodPresets;
  const modeRuleDevice = devices.find(
    (device) => (device.id ?? device.deviceId) === modeRuleDeviceId
  );
  const modeRuleProperties = modeRuleDevice
    ? [
      "policy",
      ...(["AC", "Fan", "Heater"].includes(modeRuleDevice.type) ? ["setting"] : []),
      ...(modeRuleDevice.capabilities?.includes("brightness") ? ["brightness"] : []),
      ...(modeRuleDevice.capabilities?.includes("color") ? ["color"] : []),
      ...(modeRuleDevice.capabilities?.includes("colorTemperature")
        ? ["colorTemperature"]
        : []),
    ]
    : [];

  const saveModePresetOverrides = async (event) => {
    event.preventDefault();
    if (!modePresetType || !modePresetId || !modeRuleDeviceId ||
        !modeRuleProperty || modeRuleValue === "") {
      setModeError("Choose a preset, device, supported property, and value.");
      return;
    }
    setModePresetBusy(true);
    setModeError("");
    setModeMessage("");
    try {
      const presets = modePresetType === "season" ? seasonPresets : moodPresets;
      const preset = presets.find((item) => item.id === modePresetId);
      const property = modeRuleProperty;
      let value = modeRuleValue;
      if (property === "policy") value = modeRuleValue;
      else if (property !== "color") value = Number(modeRuleValue);
      const nextOverrides = [
        ...(preset?.overrides ?? []).filter(
          (rule) => !(rule.deviceId === modeRuleDeviceId && rule.property === property)
        ),
        { deviceId: modeRuleDeviceId, property, value },
      ];
      await fetchJson(
        `${API_URL}/modes/presets/${modePresetType}/${encodeURIComponent(modePresetId)}`,
        {
          method: "PUT",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ overrides: nextOverrides }),
        }
      );
      setModeRuleValue("");
      setModeMessage(`Saved a device-specific customization for ${preset?.label ?? modePresetId}.`);
      await loadModeData();
      setModePreview(null);
    } catch (err) {
      setModeError(`Could not save the preset customization: ${err.message}`);
    } finally {
      setModePresetBusy(false);
    }
  };

  const removeModePresetOverride = async (presetType, presetId, deviceId, property) => {
    const presets = presetType === "season" ? seasonPresets : moodPresets;
    const preset = presets.find((item) => item.id === presetId);
    if (!preset) return;
    setModePresetBusy(true);
    setModeError("");
    try {
      const overrides = preset.overrides.filter(
        (rule) => !(rule.deviceId === deviceId && rule.property === property)
      );
      await fetchJson(`${API_URL}/modes/presets/${presetType}/${encodeURIComponent(presetId)}`, {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ overrides }),
      });
      setModeMessage(`Removed the ${property} customization from ${preset.label}.`);
      await loadModeData();
      setModePreview(null);
    } catch (err) {
      setModeError(`Could not remove the preset customization: ${err.message}`);
    } finally {
      setModePresetBusy(false);
    }
  };

  const activePreferenceRoom = rooms.some(
    (room) => getRoomLabel(room) === preferenceRoom
  )
    ? preferenceRoom
    : getRoomLabel(rooms[0]);
  const activePreferenceDevices = devices.filter(
    (device) => (device.roomName ?? device.room ?? "Unassigned") === activePreferenceRoom
  );
  const preferenceKey = JSON.stringify([selectedProfileId, activePreferenceRoom]);
  const preferencesReady = preferencesForKey === preferenceKey;
  const activePreferenceError =
    preferenceErrorForKey === preferenceKey ? preferenceError : "";

  useEffect(() => {
    const key = JSON.stringify([selectedProfileId, activePreferenceRoom]);
    const controller = new AbortController();

    if (!selectedProfileId || !activePreferenceRoom) {
      return () => controller.abort();
    }

    const query = new URLSearchParams({ roomName: activePreferenceRoom });

    fetch(
      `${API_URL}/profiles/${encodeURIComponent(selectedProfileId)}/preferences?${query}`,
      { signal: controller.signal }
    )
      .then(async (response) => {
        let data = {};
        try {
          data = await response.json();
        } catch {
          if (response.ok) throw new Error("The server returned an invalid response.");
        }

        if (response.status === 404 &&
            data.error === "No saved preferences found for this profile and room.") {
            setPreferences(EMPTY_PREFERENCES);
            setPreferencesForKey(key);
            setPreferencesSaved(false);
            setPreferenceMessage("No saved preferences for this profile and room yet.");
            return;
        }

        if (!response.ok) {
          throw new Error(data.error || data.message || `Request failed (${response.status})`);
        }
        setPreferences({
          acTemperature: Number(data.acTemperature),
          heaterTemperature: Number(data.heaterTemperature),
          fanSpeed: Number(data.fanSpeed),
          lightOn: Number(data.lightOn),
          automaticApplyEnabled: Boolean(data.automaticApplyEnabled),
          firstApplicationConfirmed: Boolean(data.firstApplicationConfirmed),
        });
        setPreferencesForKey(key);
        setPreferencesSaved(true);
        setPreferenceMessage("Saved preferences loaded.");
      })
      .catch((err) => {
        if (err.name !== "AbortError") {
          setPreferenceError(`Could not load preferences: ${err.message}`);
          setPreferenceErrorForKey(key);
        }
      })
      .finally(() => {
        if (!controller.signal.aborted) {
          setPreferencesLoading(false);
        }
      });

    return () => controller.abort();
  }, [selectedProfileId, activePreferenceRoom]);

  const selectProfile = (profileId) => {
    setSelectedProfileId(profileId);
    setPreferencesLoading(Boolean(profileId && activePreferenceRoom));
    setPreferenceMessage("");
    setPreferenceError("");
    setApplyResult(null);
  };

  const selectPreferenceRoom = (roomName) => {
    setPreferenceRoom(roomName);
    setPreferencesLoading(Boolean(selectedProfileId && roomName));
    setPreferenceMessage("");
    setPreferenceError("");
    setApplyResult(null);
  };

  const updatePreference = (name, value) => {
    setPreferences((current) => ({ ...current, [name]: Number(value) }));
    setPreferencesSaved(false);
    setPreferenceMessage("");
    setApplyResult(null);
  };

  const updateAutomaticApply = (enabled) => {
    setPreferences((current) => ({
      ...current,
      automaticApplyEnabled: enabled,
    }));
    setPreferencesSaved(false);
    setPreferenceMessage("");
    setApplyResult(null);
  };

  const savePreferences = async (event) => {
    event.preventDefault();

    if (!selectedProfileId || !activePreferenceRoom || !preferencesReady) return;

    setPreferencesLoading(true);
    setPreferenceError("");
    setPreferenceMessage("");

    try {
      const data = await fetchJson(
        `${API_URL}/profiles/${encodeURIComponent(selectedProfileId)}/preferences`,
        {
          method: "PUT",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            roomName: activePreferenceRoom,
            acTemperature: preferences.acTemperature,
            heaterTemperature: preferences.heaterTemperature,
            fanSpeed: preferences.fanSpeed,
            lightOn: preferences.lightOn,
            automaticApplyEnabled: preferences.automaticApplyEnabled,
          }),
        }
      );

      setPreferences({
        acTemperature: Number(data.acTemperature),
        heaterTemperature: Number(data.heaterTemperature),
        fanSpeed: Number(data.fanSpeed),
        lightOn: Number(data.lightOn),
        automaticApplyEnabled: Boolean(data.automaticApplyEnabled),
        firstApplicationConfirmed: Boolean(data.firstApplicationConfirmed),
      });
      setPreferencesForKey(preferenceKey);
      setPreferencesSaved(true);
      setPreferenceErrorForKey("");
      setPreferenceMessage("Preferences saved for this profile and room.");
    } catch (err) {
      setPreferenceError(`Could not save preferences: ${err.message}`);
    } finally {
      setPreferencesLoading(false);
    }
  };

  const applyPreferences = async () => {
    if (!selectedProfileId || !activePreferenceRoom || !preferencesSaved) return;

    const preview = preferencePreview.map((action) =>
      action.deviceName
        ? `${action.label} → ${action.deviceName}: ${action.valueLabel}`
        : `${action.label}: ${action.valueLabel} (no matching appliance)`
    ).join("\n");

    if (!window.confirm(`Apply these saved preferences to ${activePreferenceRoom}?\n\n${preview}`)) {
      return;
    }

    setApplyBusy(true);
    setPreferenceError("");
    setApplyResult(null);

    try {
      const result = await fetchJson(
        `${API_URL}/profiles/${encodeURIComponent(selectedProfileId)}/preferences/apply`,
        {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({
            roomName: activePreferenceRoom,
            confirmed: true,
          }),
        }
      );

      setApplyResult(result);
      await loadDashboard(false);
      await loadEnergySessions();
      await loadEnergySummary();
    } catch (err) {
      setPreferenceError(`Could not apply preferences: ${err.message}`);
    } finally {
      setApplyBusy(false);
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
  }, [fetchJson]);

  const loadEnergySummary = useCallback(async () => {
    try {
      const data = await fetchJson(`${API_URL}/energy/summary`);
      setEnergySummary(data);
      setEnergySummaryError("");
    } catch (err) {
      setEnergySummaryError(`Unable to load energy summaries: ${err.message}`);
    } finally {
      setEnergySummaryLoading(false);
    }
  }, [fetchJson]);

  useEffect(() => {
    const initialLoad = setTimeout(() => {
      loadDashboard(true);
      loadEnergySessions();
      loadEnergySummary();
      loadProfiles();
    }, 0);

    const interval = setInterval(() => {
      loadDashboard(false);
      loadEnergySessions();
      loadEnergySummary();
    }, 10000);

    return () => {
      clearTimeout(initialLoad);
      clearInterval(interval);
    };
  }, [loadDashboard, loadEnergySessions, loadEnergySummary, loadProfiles]);

  useEffect(() => {
    const interval = setInterval(
      () => setNowSeconds(Math.floor(Date.now() / 1000)),
      1000
    );

    return () => clearInterval(interval);
  }, []);

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

  const preferencePreview = useMemo(() => {
    if (!preferencesReady) return [];

    const settings = [
      ["acTemperature", "AC temperature", "AC", (value) => `${value} °C`],
      ["heaterTemperature", "Heater temperature", "Heater", (value) => `${value} °C`],
      ["fanSpeed", "Fan speed", "Fan", (value) => `Speed ${value}`],
      ["lightOn", "Light preference", "Light", (value) => value ? "Prefer ON" : "Prefer OFF"],
    ];

    return settings.flatMap(([key, label, type, formatValue]) => {
      const value = preferences[key];
      if (value === -1) return [];

      const supported = devices.filter((device) =>
        getRoomName(device) === activePreferenceRoom &&
        String(device.type ?? device.deviceType ?? "") === type
      );

      if (supported.length === 0) {
        return [{
          key,
          label,
          valueLabel: formatValue(value),
          deviceName: "",
        }];
      }

      return supported.map((device) => ({
        key,
        label,
        valueLabel: formatValue(value),
        deviceName: getDeviceName(device),
      }));
    });
  }, [devices, activePreferenceRoom, preferences, preferencesReady]);

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
      const updated = await fetchJson(
        `${API_URL}/devices/${encodeURIComponent(id)}/power`,
        {
          method: "POST",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ on: turnOn }),
        }
      );

      deviceRefreshVersion.current += 1;
      setDevices((current) => current.map((item) =>
        getDeviceId(item) === id ? { ...item, ...updated } : item
      ));
      await loadEnergySessions();
      await loadEnergySummary();
    } catch (err) {
      setError(`Could not update ${getDeviceName(device)}: ${err.message}`);
    } finally {
      setUpdatingDevices((current) => ({ ...current, [id]: false }));
    }
  };

  const updateDeviceSetting = async (device, property, value) => {
    const id = getDeviceId(device);
    const key = `${id}:${property}`;
    setDeviceSettingBusy((current) => ({ ...current, [key]: true }));
    setDeviceSettingErrors((current) => ({ ...current, [id]: "" }));
    try {
      const response = await fetch(
        `${API_URL}/devices/${encodeURIComponent(id)}/settings`,
        {
          method: "PATCH",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ [property]: value }),
        }
      );
      if (!response.ok) throw new Error(await getApiError(response));
      const updated = await response.json();
      deviceRefreshVersion.current += 1;
      setDevices((current) => current.map((item) =>
        getDeviceId(item) === id ? { ...item, ...updated } : item
      ));
      setDeviceSettingDrafts((current) => {
        const next = { ...current };
        delete next[key];
        return next;
      });
    } catch (err) {
      setDeviceSettingErrors((current) => ({
        ...current,
        [id]: `Could not update ${getDeviceName(device)}: ${err.message}`,
      }));
    } finally {
      setDeviceSettingBusy((current) => ({ ...current, [key]: false }));
    }
  };

  const updateDeviceDetails = async (device) => {
    const id = getDeviceId(device);
    const name = editingDeviceName.trim();
    const powerWatts = Number(editingDevicePower);
    if (!name || name.length > 100 || !Number.isInteger(powerWatts) ||
        powerWatts < 1 || powerWatts > 100000)
    {
      setDeviceSettingErrors((current) => ({
        ...current,
        [id]: "Name must be 1–100 characters and rated power must be 1–100000 W.",
      }));
      return;
    }
    setUpdatingDevices((current) => ({ ...current, [id]: true }));
    setDeviceSettingErrors((current) => ({ ...current, [id]: "" }));
    try {
      const response = await fetch(
        `${API_URL}/devices/${encodeURIComponent(id)}`,
        {
          method: "PATCH",
          headers: { "Content-Type": "application/json" },
          body: JSON.stringify({ name, powerWatts }),
        }
      );
      if (!response.ok) throw new Error(await getApiError(response));
      const updated = await response.json();
      deviceRefreshVersion.current += 1;
      setDevices((current) => current.map((item) =>
        getDeviceId(item) === id ? { ...item, ...updated } : item
      ));
      setEditingDevice(null);
      await loadEnergySessions();
      await loadEnergySummary();
    } catch (err) {
      setDeviceSettingErrors((current) => ({
        ...current,
        [id]: `Could not update device: ${err.message}`,
      }));
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
      await loadEnergySummary();
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
      initialOn: deviceForm.initialOn,
      temporary: deviceForm.temporary,
      lifetimeSeconds,
    };
    if (deviceForm.type === "AC" || deviceForm.type === "Fan" ||
        deviceForm.type === "Heater")
      payload.setting = Number(deviceForm.setting);
    if (deviceForm.type === "Light") {
      payload.lightType = deviceForm.lightType;
      if (["dimmable", "rgb", "tunable-white", "smart-bulb"].includes(deviceForm.lightType))
        payload.brightness = Number(deviceForm.brightness);
      if (["rgb", "smart-bulb"].includes(deviceForm.lightType))
        payload.color = deviceForm.color;
      if (["tunable-white", "smart-bulb"].includes(deviceForm.lightType))
        payload.colorTemperature = Number(deviceForm.colorTemperature);
    }

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
        initialOn: false,
        temporary: true,
        lifetimeValue: "30",
        lifetimeUnit: "minutes",
        setting: "24",
        lightType: "standard",
        brightness: "100",
        color: "#FFFFFF",
        colorTemperature: "4000",
      });

      deviceRefreshVersion.current += 1;
      await loadDashboard(false);
      await loadEnergySessions();
      await loadEnergySummary();
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
    await loadEnergySummary();
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

  const renderDeviceControls = (device) => {
    const id = getDeviceId(device);
    const type = String(device.type ?? "");
    const settingKey = `${id}:setting`;
    const currentSetting = Number(device.setting);
    const draftSetting = deviceSettingDrafts[settingKey] ?? String(currentSetting);
    const settingBusy = Boolean(deviceSettingBusy[settingKey]);
    const rangeSetting = (label, min, max, unit) => (
      <div className="device-setting-control" key="setting">
        <label htmlFor={`${id}-setting`}>
          <span>{label}</span>
          <strong>{draftSetting}{unit}</strong>
        </label>
        <input
          id={`${id}-setting`}
          type="range"
          min={min}
          max={max}
          step="1"
          value={draftSetting}
          onChange={(event) => setDeviceSettingDrafts((current) => ({
            ...current,
            [settingKey]: event.target.value,
          }))}
          aria-label={`${getDeviceName(device)} ${label}`}
        />
        <div className="device-setting-footer">
          <small>{min}{unit} – {max}{unit}{type === "Fan" && !isDeviceOn(device) ? " · configurable while OFF" : ""}</small>
          <button
            className="text-button"
            type="button"
            disabled={settingBusy || Number(draftSetting) === currentSetting}
            onClick={() => updateDeviceSetting(device, "setting", Number(draftSetting))}
          >{settingBusy ? "Saving…" : "Save"}</button>
        </div>
      </div>
    );

    if (type === "AC")
      return rangeSetting("Target temperature", 16, 30, " °C");
    if (type === "Heater")
      return rangeSetting("Target temperature", 20, 45, " °C");
    if (type === "Fan")
      return rangeSetting("Speed", 1, 5, "");

    if (type !== "Light")
      return <p className="device-control-note">Basic power control only.</p>;

    const capabilities = Array.isArray(device.capabilities)
      ? device.capabilities
      : [];
    if (capabilities.length === 0)
      return <p className="device-control-note">{device.lightType ?? "standard"} light · no adjustable settings.</p>;

    const controls = [];
    if (capabilities.includes("brightness")) {
      const key = `${id}:brightness`;
      const value = deviceSettingDrafts[key] ?? String(device.brightness);
      controls.push(
        <div className="device-setting-control" key="brightness">
          <label htmlFor={`${id}-brightness`}><span>Brightness</span><strong>{value}%</strong></label>
          <input id={`${id}-brightness`} type="range" min="0" max="100" step="1" value={value} aria-label={`${getDeviceName(device)} brightness`} onChange={(event) => setDeviceSettingDrafts((current) => ({ ...current, [key]: event.target.value }))} />
          <div className="device-setting-footer"><small>0–100%</small><button className="text-button" type="button" disabled={deviceSettingBusy[key] || Number(value) === Number(device.brightness)} onClick={() => updateDeviceSetting(device, "brightness", Number(value))}>{deviceSettingBusy[key] ? "Saving…" : "Save"}</button></div>
        </div>
      );
    }
    if (capabilities.includes("color")) {
      const key = `${id}:color`;
      const value = deviceSettingDrafts[key] ?? device.color;
      controls.push(
        <div className="device-setting-control" key="color">
          <label htmlFor={`${id}-color`}><span>RGB color</span><strong>{value}</strong></label>
          <input className="device-color-input" id={`${id}-color`} type="color" value={value} aria-label={`${getDeviceName(device)} color`} onChange={(event) => setDeviceSettingDrafts((current) => ({ ...current, [key]: event.target.value }))} />
          <div className="device-setting-footer"><small>Selected color, retained when OFF</small><button className="text-button" type="button" disabled={deviceSettingBusy[key] || value.toUpperCase() === String(device.color).toUpperCase()} onClick={() => updateDeviceSetting(device, "color", value)}>{deviceSettingBusy[key] ? "Saving…" : "Save"}</button></div>
        </div>
      );
    }
    if (capabilities.includes("colorTemperature")) {
      const key = `${id}:colorTemperature`;
      const value = deviceSettingDrafts[key] ?? String(device.colorTemperature);
      controls.push(
        <div className="device-setting-control" key="color-temperature">
          <label htmlFor={`${id}-color-temperature`}><span>Color temperature</span><strong>{value} K</strong></label>
          <input id={`${id}-color-temperature`} type="range" min="2700" max="6500" step="100" value={value} aria-label={`${getDeviceName(device)} color temperature`} onChange={(event) => setDeviceSettingDrafts((current) => ({ ...current, [key]: event.target.value }))} />
          <div className="device-setting-footer"><small>2700–6500 K</small><button className="text-button" type="button" disabled={deviceSettingBusy[key] || Number(value) === Number(device.colorTemperature)} onClick={() => updateDeviceSetting(device, "colorTemperature", Number(value))}>{deviceSettingBusy[key] ? "Saving…" : "Save"}</button></div>
        </div>
      );
    }
    return <div className="device-setting-controls">{controls}</div>;
  };

  const goTo = (section) => {
    const runningRoomSession =
      cameraSessionRef.current || cameraStarting;
    const enrollmentPreview =
      activeSection === "camera" && cameraStreamRef.current;
    if ((runningRoomSession && section !== "devices") ||
        (enrollmentPreview && section !== "camera"))
    {
      void stopCamera();
    }
    if (section === "devices" && !manualCameraOverrideRef.current)
      cameraAutoStartAttemptRef.current = "";
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
  const targetModeDevices = devices.filter((device) =>
    modeTargetRooms.length === 1 &&
    modeTargetRooms[0] === getRoomName(device)
  );
  const activeModePreset = (modePresetType === "season"
    ? seasonPresets
    : moodPresets
  ).find((preset) => preset.id === modePresetId);
  const modeResultActions = modeApplicationResult?.actions ?? [];
  const modeResultCounts = {
    turnedOn: modeResultActions.filter(
      (action) => action.property === "power" &&
        action.status === "applied" && action.value === true
    ).length,
    turnedOff: modeResultActions.filter(
      (action) => action.property === "power" &&
        action.status === "applied" && action.value === false
    ).length,
    configured: modeResultActions.filter(
      (action) => !["power", "policy"].includes(action.property) &&
        action.status === "applied"
    ).length,
    unchanged: modeResultActions.filter(
      (action) => action.status === "unchanged"
    ).length,
    failed: modeResultActions.filter(
      (action) => action.status === "failed"
    ).length,
  };
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
            ["modes", "◐", "Season & Mood Modes"],
            ["energy", "ϟ", "Energy monitor"],
            ["assistant", "✦", "Energy assistant"],
            ["camera", "◉", "Face enrollments"],
            ["billing", "₹", "Bill estimator"],
            ["history", "◷", "Session history"],
            ["profiles", "♙", "Profiles & preferences"],
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
              <button className="room-nav-item" key={name} onClick={() => { void changeSelectedRoom(name); goTo("devices"); }}>
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
            <button className="icon-button" aria-label="Refresh dashboard" title="Refresh dashboard" onClick={() => { loadDashboard(true); loadEnergySessions(); loadEnergySummary(); }}>↻</button>
            <div className="avatar">S</div>

            <label className="profile-selector">
              <span>👤 User</span>
              <select
                value={selectedProfileId}
                onChange={(event) => selectProfile(event.target.value)}
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


          <section className="stats-grid" aria-label="Home statistics">
            {stat("Total devices", devices.length, "Registered across your home", "⌁", "purple")}
            {stat("Devices online", `${activeDevices}/${devices.length}`, "Currently switched on", "ϟ", "green")}
            {stat("Active power", <>{(totalPowerWatts / 1000).toFixed(2)} <small>kW</small></>, `${totalPowerWatts.toLocaleString("en-IN")} watts in use`, "ϟ", "amber")}
            {stat("Rooms", rooms.length, "Configured spaces", "⌂", "blue")}
          </section>

        </>}

        {activeSection === "profiles" && <section id="profiles" className="content-section">
          <div className="section-heading">
            <div>
              <p className="eyebrow">PERSONAL COMFORT</p>
              <h2>Profiles &amp; preferences</h2>
              <p className="section-subtitle">Saved preferences are separate from appliance state. Selecting a profile never changes devices.</p>
            </div>
          </div>

          <form className="inline-create panel" onSubmit={createProfile}>
            <label className="grow-field">
              Create a profile
              <input
                value={profileNameInput}
                onChange={(event) => setProfileNameInput(event.target.value)}
                placeholder="Enter a profile name"
                maxLength={80}
                required
              />
            </label>
            <button type="submit" className="primary-button" disabled={profileBusy}>
              {profileBusy ? "Creating…" : "＋ Create profile"}
            </button>
          </form>
          {profileMessage && (
            <div className={`form-message ${profileMessage.startsWith("Could not") ? "is-error" : "is-success"}`} role="status">
              {profileMessage}
            </div>
          )}

          <div className="profile-list-heading">
            <h3>Saved profiles</h3>
            {profilesLoading && <span>Loading profiles…</span>}
          </div>
          {profiles.length === 0 && !profilesLoading ? (
            <div className="empty-state">
              <span>♙</span>
              <h3>No profiles available</h3>
              <p>Create a profile above. If profile loading failed, the error is shown above.</p>
            </div>
          ) : (
            <div className="profile-list">
              {profiles.map((profile) => (
                <button
                  type="button"
                  key={profile.id}
                  className={`profile-card ${selectedProfileId === profile.id ? "profile-card-selected" : ""}`}
                  onClick={() => selectProfile(profile.id)}
                  aria-pressed={selectedProfileId === profile.id}
                >
                  <span className="profile-card-avatar">{profile.displayName.slice(0, 1).toUpperCase()}</span>
                  <span><strong>{profile.displayName}</strong><small>{profile.id}</small></span>
                  {selectedProfileId === profile.id && <span className="profile-current">Selected</span>}
                </button>
              ))}
            </div>
          )}

          {selectedProfileId && (
            <div className="preferences-workspace">
              <div className="selected-profile panel">
                <div>
                  <p className="eyebrow">SELECTED PROFILE</p>
                  <h3>{profiles.find((profile) => profile.id === selectedProfileId)?.displayName ?? selectedProfileId}</h3>
                </div>
                <label className="preference-room-select">
                  Room
                  <select value={activePreferenceRoom} onChange={(event) => selectPreferenceRoom(event.target.value)} disabled={rooms.length === 0}>
                    <option value="">Select a room</option>
                    {rooms.map((room) => {
                      const name = getRoomLabel(room);
                      return name ? <option key={name} value={name}>{name}</option> : null;
                    })}
                  </select>
                </label>
              </div>

              {!activePreferenceRoom ? (
                <div className="empty-state"><span>⌂</span><h3>No rooms available</h3><p>Create a room before setting room preferences.</p></div>
              ) : (
                <>
                  {(preferencesLoading || (!preferencesReady && !activePreferenceError)) ? (
                    <div className="panel preference-status" role="status"><div className="loading-orb" />Loading preferences for {activePreferenceRoom}…</div>
                  ) : null}
                  {activePreferenceError && <div className="alert alert-error" role="alert">{activePreferenceError}</div>}
                  {preferencesReady && preferenceMessage && <div className="form-message is-success" role="status">{preferenceMessage}</div>}

                  <form className="panel preference-form" onSubmit={savePreferences}>
                    <div className="form-heading">
                    <div><p className="eyebrow">SAVED DESIRED SETTINGS</p><h3>{activePreferenceRoom}</h3></div>
                      <span className="preference-note">Unspecified values are not applied.</span>
                    </div>
                    <div className="form-grid preference-grid">
                      {activePreferenceDevices.some((device) =>
                        String(device.type ?? device.deviceType).toLowerCase() === "ac"
                      ) && <label>Air conditioner temperature
                        <select
                          value={preferencesReady ? preferences.acTemperature : -1}
                          disabled={!preferencesReady || preferencesLoading}
                          onChange={(event) => updatePreference("acTemperature", event.target.value)}
                        >
                          <option value={-1}>No preference</option>
                          {Array.from({ length: 15 }, (_, index) => index + 16).map((value) => <option key={value} value={value}>{value} °C</option>)}
                        </select>
                      </label>}
                      {activePreferenceDevices.some((device) =>
                        String(device.type ?? device.deviceType).toLowerCase() === "heater"
                      ) && <label>Heater temperature
                        <select
                          value={preferencesReady ? preferences.heaterTemperature : -1}
                          disabled={!preferencesReady || preferencesLoading}
                          onChange={(event) => updatePreference("heaterTemperature", event.target.value)}
                        >
                          <option value={-1}>No preference</option>
                          {Array.from({ length: 26 }, (_, index) => index + 20).map((value) => <option key={value} value={value}>{value} °C</option>)}
                        </select>
                      </label>}
                      {activePreferenceDevices.some((device) =>
                        String(device.type ?? device.deviceType).toLowerCase() === "fan"
                      ) && <label>Fan speed
                        <select
                          value={preferencesReady ? preferences.fanSpeed : -1}
                          disabled={!preferencesReady || preferencesLoading}
                          onChange={(event) => updatePreference("fanSpeed", event.target.value)}
                        >
                          <option value={-1}>No preference</option>
                          {Array.from({ length: 5 }, (_, index) => index + 1).map((value) => <option key={value} value={value}>Speed {value}</option>)}
                        </select>
                      </label>}
                      {activePreferenceDevices.some((device) =>
                        String(device.type ?? device.deviceType).toLowerCase() === "light"
                      ) && <label>Light preference
                        <select
                          value={preferencesReady ? preferences.lightOn : -1}
                          disabled={!preferencesReady || preferencesLoading}
                          onChange={(event) => updatePreference("lightOn", event.target.value)}
                        >
                          <option value={-1}>No preference</option>
                          <option value={0}>Prefer OFF</option>
                          <option value={1}>Prefer ON</option>
                        </select>
                      </label>}
                      {activePreferenceDevices.length === 0 &&
                        <p className="fine-print">No supported devices are registered in this room. You can still save the profile and camera preferences.</p>}
                    </div>
                    <p className="fine-print">Supported ranges come from the appliance controls: AC 16–30 °C, heater 20–45 °C, and fan speed 1–5.</p>
                    <label className="checkbox-label">
                      <input
                        type="checkbox"
                        checked={preferencesReady && preferences.automaticApplyEnabled}
                        disabled={!preferencesReady || preferencesLoading}
                        onChange={(event) => updateAutomaticApply(event.target.checked)}
                      />
                      Automatically apply these preferences when this profile is recognized in this room after I confirm the first application.
                    </label>
                    {preferences.firstApplicationConfirmed && preferences.automaticApplyEnabled &&
                      <p className="fine-print">First application confirmed. Later recognized visits to this room may automatically apply these saved settings.</p>}
                    <div className="form-actions">
                      <button type="submit" className="primary-button" disabled={!preferencesReady || preferencesLoading}>
                        {preferencesLoading ? "Saving…" : "Save preferences"}
                      </button>
                      {selectedProfileId && activePreferenceRoom &&
                        <button
                          type="button"
                          className="secondary-button"
                          onClick={() => {
                            setEnrollmentProfileId(selectedProfileId);
                            goTo("camera");
                          }}
                        >Enroll this profile for local recognition</button>}
                    </div>
                  </form>

                  <section className="panel apply-preview">
                    <div className="form-heading">
                      <div><p className="eyebrow">USER-APPROVED DEVICE CHANGES</p><h3>Apply preferences</h3></div>
                      <button type="button" className="primary-button" disabled={!preferencesReady || !preferencesSaved || preferencePreview.length === 0 || applyBusy} onClick={applyPreferences}>
                        {applyBusy ? "Applying…" : "Apply saved preferences"}
                      </button>
                    </div>
                    {!preferencesSaved && <p className="fine-print">Save the current values before applying them. Applying is a separate action and requires confirmation.</p>}
                    {preferencesReady && preferencesSaved && preferencePreview.length === 0 && <p className="fine-print">There are no specified preferences to apply.</p>}
                    {preferencePreview.length > 0 && (
                      <ul className="apply-action-list">
                        {preferencePreview.map((action, index) => (
                          <li key={`${action.key}-${action.deviceName || "missing"}-${index}`}>
                            <span><strong>{action.label}</strong><small>{action.deviceName || "No matching appliance in this room"}</small></span>
                            <span className={action.deviceName ? "action-supported" : "action-unsupported"}>{action.valueLabel}{action.deviceName ? "" : " · unsupported"}</span>
                          </li>
                        ))}
                      </ul>
                    )}
                    {applyResult && (
                      <div className="apply-results" role="status">
                        <strong>{applyResult.success ? "All requested changes applied." : "Some preferences could not be applied."}</strong>
                        <ul className="apply-action-list">
                          {(applyResult.actions ?? []).map((action, index) => (
                            <li key={`${action.setting}-${action.deviceId ?? "missing"}-${index}`}>
                              <span><strong>{action.deviceName ?? action.deviceType ?? action.setting}</strong><small>{action.message ?? action.setting}</small></span>
                              <span className={action.status === "applied" ? "action-supported" : "action-unsupported"}>{action.status}</span>
                            </li>
                          ))}
                        </ul>
                      </div>
                    )}
                  </section>
                </>
              )}
            </div>
          )}
        </section>}

        {activeSection === "devices" && <section id="devices" className="content-section">
          <div className="section-heading">
            <div><p className="eyebrow">CONTROL CENTER</p><h2>Devices <span className="count-chip">{filteredDevices.length}</span></h2><p className="section-subtitle">Your connected appliances, all in one place.</p></div>
            <div className="bulk-actions">
              {(selectedRoom === "All Rooms" || roomOperatingMode === "manual") && <>
                <button className="secondary-button" disabled={bulkUpdating} onClick={() => bulkToggle(true)}>Power all on</button>
                <button className="secondary-button" disabled={bulkUpdating} onClick={() => bulkToggle(false)}>Power all off</button>
              </>}
              <button className="primary-button" onClick={() => { setShowDeviceForm((v) => !v); setDeviceMessage(""); }}>{showDeviceForm ? "Cancel" : "＋ Add device"}</button>
            </div>
          </div>

          {showDeviceForm && (
            <form className="device-form panel" onSubmit={handleAddDevice}>
              <div className="form-heading"><div><p className="eyebrow">NEW CONNECTION</p><h3>Add a device</h3></div><button type="button" className="icon-button" onClick={() => setShowDeviceForm(false)} aria-label="Close form">×</button></div>
              <div className="form-grid">
                <label>Device ID<input value={deviceForm.id} onChange={(e) => setDeviceForm({ ...deviceForm, id: e.target.value })} placeholder="e.g. BEDROOM_LIGHT_02" required /></label>
                <label>Display name<input value={deviceForm.name} onChange={(e) => setDeviceForm({ ...deviceForm, name: e.target.value })} placeholder="e.g. Bedside lamp" required /></label>
                <label>Device type<select value={deviceForm.type} onChange={(e) => setDeviceForm({ ...deviceForm, type: e.target.value, setting: e.target.value === "Fan" ? "1" : e.target.value === "Heater" ? "25" : "24" })}><option>Light</option><option>Fan</option><option>AC</option><option>Heater</option><option>Other</option></select></label>
                <label>Room<select value={deviceForm.roomName} onChange={(e) => setDeviceForm({ ...deviceForm, roomName: e.target.value })} required><option value="">Select a room</option>{rooms.map((room) => { const name = typeof room === "string" ? room : room.name ?? room.roomName; return name ? <option key={name} value={name}>{name}</option> : null; })}</select></label>
                <label>Rated power (watts)<input type="number" min="1" max="100000" value={deviceForm.powerWatts} onChange={(e) => setDeviceForm({ ...deviceForm, powerWatts: e.target.value })} required /></label>
                {deviceForm.type === "AC" && <label>Target temperature (16–30 °C)<input type="number" min="16" max="30" value={deviceForm.setting} onChange={(e) => setDeviceForm({ ...deviceForm, setting: e.target.value })} required /></label>}
                {deviceForm.type === "Fan" && <label>Fan speed (1–5){deviceForm.setting}<input type="range" min="1" max="5" step="1" value={deviceForm.setting} onChange={(e) => setDeviceForm({ ...deviceForm, setting: e.target.value })} /></label>}
                {deviceForm.type === "Heater" && <label>Target temperature (20–45 °C)<input type="number" min="20" max="45" value={deviceForm.setting} onChange={(e) => setDeviceForm({ ...deviceForm, setting: e.target.value })} required /></label>}
                {deviceForm.type === "Light" && <label>Light capability<select value={deviceForm.lightType} onChange={(e) => setDeviceForm({ ...deviceForm, lightType: e.target.value })}><option value="standard">Standard white</option><option value="warm-white">Warm white</option><option value="cool-white">Cool white</option><option value="dimmable">Dimmable</option><option value="rgb">RGB color-changing</option><option value="tunable-white">Tunable white</option><option value="smart-bulb">Smart bulb (brightness, RGB and white temperature)</option></select></label>}
                {deviceForm.type === "Light" && ["dimmable", "rgb", "tunable-white", "smart-bulb"].includes(deviceForm.lightType) && <label>Initial brightness: {deviceForm.brightness}%<input type="range" min="0" max="100" step="1" value={deviceForm.brightness} onChange={(e) => setDeviceForm({ ...deviceForm, brightness: e.target.value })} /></label>}
                {deviceForm.type === "Light" && ["rgb", "smart-bulb"].includes(deviceForm.lightType) && <label>Initial RGB color<input type="color" value={deviceForm.color} onChange={(e) => setDeviceForm({ ...deviceForm, color: e.target.value })} /></label>}
                {deviceForm.type === "Light" && ["tunable-white", "smart-bulb"].includes(deviceForm.lightType) && <label>Initial color temperature: {deviceForm.colorTemperature} K<input type="range" min="2700" max="6500" step="100" value={deviceForm.colorTemperature} onChange={(e) => setDeviceForm({ ...deviceForm, colorTemperature: e.target.value })} /></label>}
                <label className="checkbox-label"><input type="checkbox" checked={deviceForm.initialOn} onChange={(e) => setDeviceForm({ ...deviceForm, initialOn: e.target.checked })} /> Start powered ON</label>
                <div className="temporary-field"><label className="checkbox-label"><input type="checkbox" checked={deviceForm.temporary} onChange={(e) => setDeviceForm({ ...deviceForm, temporary: e.target.checked })} /> Temporary device</label>{deviceForm.temporary && <div className="lifetime-row"><input aria-label="Device lifetime" type="number" min="1" value={deviceForm.lifetimeValue} onChange={(e) => setDeviceForm({ ...deviceForm, lifetimeValue: e.target.value })} required /><select aria-label="Lifetime unit" value={deviceForm.lifetimeUnit} onChange={(e) => setDeviceForm({ ...deviceForm, lifetimeUnit: e.target.value })}><option value="minutes">Minutes</option><option value="hours">Hours</option></select></div>}</div>
              </div>
              {deviceMessage && <div className={`form-message ${deviceMessage.startsWith("Error") ? "is-error" : "is-success"}`} role="status">{deviceMessage}</div>}
              <div className="form-actions"><button type="button" className="secondary-button" onClick={() => setShowDeviceForm(false)}>Cancel</button><button type="submit" className="primary-button" disabled={deviceBusy}>{deviceBusy ? "Adding device…" : "Add device"}</button></div>
            </form>
          )}

          <div className="toolbar">
            <label className="search-box"><span>⌕</span><input placeholder="Search devices or rooms…" value={search} onChange={(e) => setSearch(e.target.value)} /><kbd>⌕</kbd></label>
            <label className="filter-select"><span>Room</span><select value={selectedRoom} onChange={(e) => { void changeSelectedRoom(e.target.value); }}><option>All Rooms</option>{rooms.map((room) => { const name = typeof room === "string" ? room : room.name ?? room.roomName; return name ? <option key={name} value={name}>{name}</option> : null; })}</select></label>
          </div>

          {selectedRoom !== "All Rooms" && <section className="room-mode-panel panel" aria-label={`${selectedRoom} operating mode`}>
            <div>
              <p className="eyebrow">ROOM OPERATING MODE</p>
              <h3>{selectedRoom}</h3>
              <p className="section-subtitle">Camera monitoring is off by default. Only a room you explicitly enabled may start monitoring when you open it; monitoring is always shown and can be stopped immediately.</p>
            </div>
            <div className="room-mode-options" role="group" aria-label="Room operating mode">
              <button type="button" className={`secondary-button ${roomOperatingMode === "manual" ? "mode-selected" : ""}`} aria-pressed={roomOperatingMode === "manual"} disabled={cameraStarting} onClick={() => { if (cameraSessionRef.current) void switchToManualControl(); else { manualCameraOverrideRef.current = true; setRoomOperatingMode("manual"); } }}>Manual Control</button>
              <button type="button" className={`secondary-button ${roomOperatingMode === "camera" ? "mode-selected" : ""}`} aria-pressed={roomOperatingMode === "camera"} disabled={cameraStarting} onClick={() => { manualCameraOverrideRef.current = false; cameraAutoStartAttemptRef.current = ""; setRoomOperatingMode("camera"); }}>Camera Automation</button>
            </div>
          </section>}

          {roomOperatingMode === "camera" && selectedRoom !== "All Rooms" ? (
            <section className="room-automation-grid">
              <div className="panel room-automation-main">
                <div className="room-automation-heading">
                    <div><p className="eyebrow">ROOM-SCOPED RECOGNITION</p><h3>Camera Automation · {selectedRoom}</h3><p className="section-subtitle">This room's saved opt-in controls monitoring. The session is bound to {automationSession?.roomName ?? selectedRoom}; use Stop Recognition or Manual Control to stop it.</p></div>
                  <span className={`camera-status camera-status-${cameraStatus}`} role="status"><i />{cameraStatus === "active" ? "RECOGNITION ACTIVE" : cameraStatus === "unavailable" ? "CAMERA OFFLINE" : cameraStatus.toUpperCase()}</span>
                </div>
                {automationError && <div className="alert alert-error" role="alert">{automationError}</div>}
                <div className="camera-controls">
                  {cameraStream
                    ? <video className="camera-preview" ref={cameraVideoRef} autoPlay muted playsInline aria-label={`Local camera preview for ${automationSession?.roomName ?? selectedRoom}`} />
                    : <div className="camera-preview camera-preview-placeholder"><span>◉</span><strong>{cameraStatus === "unavailable" ? "Camera Offline" : "Recognition is stopped"}</strong><small>Camera frames remain in this browser and are sent only to the loopback face service.</small></div>}
                  <div className="room-recognition-controls">
                    <div className="global-automation-toggle">
                      <span><strong>Global automation</strong><small>{automationEnabled ? "Enabled · room-level opt-in is still required" : "Disabled by default"}</small></span>
                      <button type="button" role="switch" aria-checked={automationEnabled} aria-label="Enable global camera automation" className={`toggle ${automationEnabled ? "toggle-on" : ""}`} disabled={automationBusy || cameraStarting} onClick={() => updateAutomationEnabled(!automationEnabled)}><span /></button>
                    </div>
                    <div className="global-automation-toggle">
                      <span><strong>{selectedRoom} camera monitoring</strong><small>{roomCameraSettingsBusy ? "Loading this room's saved setting…" : roomCameraEnabled ? "Enabled for this room · recognition still requires Start" : "Disabled for this room by default"}</small></span>
                      <button type="button" role="switch" aria-checked={roomCameraEnabled} aria-label={`Enable camera monitoring for ${selectedRoom}`} className={`toggle ${roomCameraEnabled ? "toggle-on" : ""}`} disabled={roomCameraSettingsBusy || roomCameraSettingsRoom !== selectedRoom || cameraStarting} onClick={() => updateRoomCameraEnabled(!roomCameraEnabled)}><span /></button>
                    </div>
                    {automationSession
                      ? <button className="secondary-button danger-button" type="button" onClick={() => { void switchToManualControl(); }}>Stop Recognition</button>
                      : <button className="primary-button" type="button" onClick={startCamera} disabled={roomCameraSettingsRoom !== selectedRoom || !automationEnabled || !roomCameraEnabled || cameraStarting || automationStatus !== "online"}>{cameraStarting ? "Starting local service…" : "Start Recognition"}</button>}
                    <p className="fine-print">Python service: {automationStatus}. Camera frames are processed locally. First application always requires preview and confirmation; later visits auto-apply only when explicitly enabled for this profile and room.</p>
                  </div>
                </div>
                <div className="occupancy-state" aria-live="polite">
                  <span>Recognition result · selected room: {automationSession?.roomName ?? selectedRoom}</span>
                  <strong>{recognition.status === "matched"
                    ? `Matched: ${automationPreview?.profileName ?? recognition.displayName}`
                    : recognition.status === "pending"
                      ? `Matching pending (${recognition.observations ?? 0}/${recognition.requiredObservations ?? 3} consistent observations)`
                      : recognition.status === "uncertain"
                        ? `Identity uncertain (${recognition.observations ?? 0}/${recognition.requiredObservations ?? 3} consistent observations)`
                      : recognition.status === "ambiguous"
                        ? "Ambiguous: multiple faces detected"
                        : recognition.status === "no_person"
                          ? "No face currently detected"
                          : recognition.status === "unknown"
                            ? "Unknown identity"
                            : recognition.status === "failed"
                              ? "Recognition failed"
                              : "Waiting for recognition"}</strong>
                  <p>{recognition.status === "ambiguous"
                    ? "No profile is selected when more than one face is present. Ensure only the consenting person is in frame; there is no automatic or arbitrary selection."
                    : recognition.status === "no_person"
                      ? "No personalized settings are eligible. This is not an occupancy or room-vacancy determination."
                      : recognition.status === "unknown"
                        ? "We don't recognize you yet. Unknown identities never load or apply household preferences."
                        : recognition.status === "uncertain"
                        ? recognition.reason === "poor_quality"
                          ? "Lighting, contrast, or focus is too poor for a reliable observation. Adjust the camera view; no profile or device changes are allowed."
                          : "The match is not reliable enough yet. No profile or device changes are allowed until observations are consistent."
                        : "Face matching is local, probabilistic, and is not liveness detection or secure authentication."}</p>
                </div>
                {recognition.status === "unknown" && <div className="form-actions">
                  <button
                    className="primary-button"
                    type="button"
                    onClick={() => {
                      setPreferenceRoom(automationSession?.roomName ?? selectedRoom);
                      goTo("profiles");
                    }}
                  >Set Up My Profile &amp; Room Preferences</button>
                  <button className="secondary-button" type="button" onClick={() => { void switchToManualControl(); }}>Continue with Manual Control</button>
                </div>}
                {recognition.status === "matched" && !automationPreview && automationError.includes("No saved preferences") && <div className="form-actions">
                  <button className="primary-button" type="button" onClick={() => {
                    setSelectedProfileId(
                      enrollments.find((item) => item.id === recognition.enrollmentId)?.profileId ?? ""
                    );
                    setPreferenceRoom(automationSession?.roomName ?? selectedRoom);
                    goTo("profiles");
                  }}>Set Up Preferences for This Room</button>
                  <button className="secondary-button" type="button" onClick={() => { void switchToManualControl(); }}>Continue with Manual Control</button>
                </div>}
                {automationPreview && <section className="panel automation-preference-preview" aria-labelledby="automation-preview-title">
                  <div className="form-heading">
                    <div><p className="eyebrow">{automationPreview.automaticApplyEnabled && automationPreview.firstApplicationConfirmed ? "AUTOMATIC APPLICATION ENABLED" : "CONFIRMATION REQUIRED"}</p><h3 id="automation-preview-title">Preference preview · {automationPreview.roomName}</h3><p>Linked profile: {automationPreview.profileName} ({automationPreview.profileId})</p></div>
                    <button type="button" className="primary-button" onClick={applyAutomationPreview} disabled={automationBusy || automationPreview.alreadyApplied || automationApplyResult?.success === true || recognition.status !== "matched" || recognition.enrollmentId !== automationPreview.enrollmentId}>{automationBusy ? "Applying…" : automationPreview.alreadyApplied ? "Already applied this session" : "Confirm and apply"}</button>
                  </div>
                  <div className="automation-preview-list">
                    {automationPreview.preview.map((item, index) => <div className="bill-row" key={`${item.deviceId ?? item.setting}-${index}`}><span>{item.deviceName ?? item.deviceType}<small>{item.setting} · {item.status}</small></span><strong>{item.previousValue ?? "—"} → {item.value ?? "—"}</strong></div>)}
                  </div>
                  {automationApplyResult && <div className={`form-message ${automationApplyResult.success ? "is-success" : "is-error"}`} role="status">{automationApplyResult.success ? "Confirmed settings were applied and recorded." : "The operation completed with unsupported or failed settings; review the per-device result."}{automationApplyResult.actions?.map((item, index) => <small key={`${item.setting}-${item.deviceId ?? index}`}>{item.deviceName ?? item.deviceType ?? item.setting}: {item.status}{item.message ? ` — ${item.message}` : ""}</small>)}</div>}
                  {recognition.status !== "matched" && <p className="fine-print">Application is paused because the currently observed identity is not a fresh single-person match.</p>}
                </section>}
                {automationEvents[0] && <p className="fine-print" role="status">Last automation event: {automationEvents[0].outcome} · {automationEvents[0].roomName} · {formatDate(automationEvents[0].eventTime)}</p>}
                <button className="secondary-button" type="button" onClick={() => { void switchToManualControl(); }}>Switch to Manual Control</button>
              </div>
            </section>
          ) : (
          loading ? <div className="empty-state"><div className="loading-orb" /><h3>Connecting to your home…</h3><p>Fetching the latest device status.</p></div> : Object.keys(deviceGroups).length === 0 ? <div className="empty-state"><span>⌁</span><h3>No devices found</h3><p>Try another search or add a device to get started.</p><button className="primary-button" onClick={() => setShowDeviceForm(true)}>＋ Add your first device</button></div> : Object.entries(deviceGroups).map(([roomName, roomDevices]) => {
            const meta = roomMeta(roomName);
            const onCount = roomDevices.filter(isDeviceOn).length;
            return <div className="room-section" key={roomName}>
              <div className="room-heading"><div className="room-title"><span className={`room-emoji ${meta.tint}`}>{meta.icon}</span><div><h3>{roomName}</h3><p>{onCount} of {roomDevices.length} devices active</p></div></div><button className="text-button" onClick={() => { void changeSelectedRoom(roomName); }}>View room →</button></div>
              <div className="device-grid">{roomDevices.map((device) => {
                const id = getDeviceId(device);
                const on = isDeviceOn(device);
                const settingsBusy = Object.entries(deviceSettingBusy).some(
                  ([key, isBusy]) => isBusy && key.startsWith(`${id}:`)
                );
                const busy = Boolean(updatingDevices[id]) || settingsBusy;
                const deleting = Boolean(deletingDevices[id]);
                const editing = editingDevice === id;
                return <article className={`device-card ${on ? "device-on" : ""}`} key={id}>
                  <div className="device-card-top"><span className={`device-icon ${on ? "icon-on" : ""}`}>{deviceMeta(device)}</span><span className={`status-tag ${on ? "status-on" : "status-off"}`}><i />{on ? "ONLINE" : "OFFLINE"}</span></div>
                  <h4>{getDeviceName(device)}</h4><p className="device-type">{device.type ?? device.deviceType ?? "Smart appliance"}</p>
                  <p className="device-room-label">{getRoomName(device)}</p>
                  <div className="device-details"><span>Rated power</span><strong>{getDevicePower(device)} W</strong></div>
                  <div className="device-card-bottom"><span className="device-id" title={id}>{id}</span><button type="button" role="switch" aria-checked={on} aria-label={`${on ? "Turn off" : "Turn on"} ${getDeviceName(device)}`} className={`toggle ${on ? "toggle-on" : ""}`} disabled={busy} onClick={() => updateDevicePower(device, !on)}><span /></button></div>
                  {renderDeviceControls(device)}
                  {deviceSettingErrors[id] && <div className="form-message is-error" role="alert">{deviceSettingErrors[id]}</div>}
                  {editing && <form className="device-edit-form" onSubmit={(event) => { event.preventDefault(); updateDeviceDetails(device); }}>
                    <label>Device name<input value={editingDeviceName} onChange={(event) => setEditingDeviceName(event.target.value)} maxLength={100} required /></label>
                    <label>Rated watts<input type="number" min="1" max="100000" value={editingDevicePower} onChange={(event) => setEditingDevicePower(event.target.value)} required /></label>
                    <div className="device-edit-actions"><button className="primary-button" type="submit" disabled={busy}>{busy ? "Saving…" : "Save changes"}</button><button className="secondary-button" type="button" onClick={() => setEditingDevice(null)}>Cancel</button></div>
                  </form>}
                  <div className="device-card-actions">
                    <button className="secondary-button" disabled={busy || deleting} onClick={() => {
                      setEditingDevice(editing ? null : id);
                      setEditingDeviceName(getDeviceName(device));
                      setEditingDevicePower(String(getDevicePower(device)));
                    }}>{editing ? "Cancel edit" : "Edit device"}</button>
                    <button className="secondary-button danger-button" disabled={busy || deleting} onClick={() => deleteDevice(device)}>{deleting ? "Deleting…" : "Remove device"}</button>
                  </div>
                </article>;
              })}</div>
            </div>;
          })
          )}
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

        {activeSection === "modes" && <section id="modes" className="content-section">
          <div className="section-heading">
            <div>
              <p className="eyebrow">USER-SELECTED SIMULATED MODES</p>
              <h2>Season &amp; Mood Modes</h2>
              <p className="section-subtitle">Choose one room, then a seasonal baseline and optional mood overlay. Review the explicit power policy and settings for every registered device before applying.</p>
            </div>
          </div>
          {modeError && <div className="alert alert-error" role="alert">{modeError}</div>}
          {modeMessage && <div className="form-message is-success" role="status">{modeMessage}</div>}
          <div className="panel mode-disclaimer">
            These are suggested, user-selected presets—not weather detection, medical or environmental advice, or control of physical appliances. Manual device controls remain available. Existing energy estimates use rated watts and recorded ON time; the current model cannot defensibly calculate energy implications from a mode setting.
          </div>

          <section className="panel mode-preset-panel" aria-labelledby="season-presets-heading">
            <div className="mode-panel-heading">
              <div><p className="eyebrow">OPTIONAL BASELINE</p><h3 id="season-presets-heading">Season</h3></div>
              {selectedSeasonId && <button className="text-button" type="button" onClick={() => { setSelectedSeasonId(""); setModePreview(null); }}>Clear season</button>}
            </div>
            <div className="mode-preset-grid">
              {seasonPresets.map((preset) => <button
                type="button"
                key={preset.id}
                className={`mode-preset-card ${selectedSeasonId === preset.id ? "mode-preset-selected" : ""}`}
                aria-pressed={selectedSeasonId === preset.id}
                onClick={() => { setSelectedSeasonId(selectedSeasonId === preset.id ? "" : preset.id); setModePreview(null); setModeApplicationResult(null); setModeConfirming(false); }}
              >
                <strong>{preset.label}</strong><span>{preset.description}</span>
                <small>{preset.overrides.length ? `${preset.overrides.length} saved device customization${preset.overrides.length === 1 ? "" : "s"}` : "Suggested defaults"}</small>
              </button>)}
            </div>
          </section>

          <section className="panel mode-preset-panel" aria-labelledby="mood-presets-heading">
            <div className="mode-panel-heading">
              <div><p className="eyebrow">OPTIONAL OVERLAY</p><h3 id="mood-presets-heading">Mood</h3></div>
              {selectedMoodId && <button className="text-button" type="button" onClick={() => { setSelectedMoodId(""); setModePreview(null); }}>Clear mood</button>}
            </div>
            <div className="mode-preset-grid mode-mood-grid">
              {moodPresets.map((preset) => <button
                type="button"
                key={preset.id}
                className={`mode-preset-card ${selectedMoodId === preset.id ? "mode-preset-selected" : ""}`}
                aria-pressed={selectedMoodId === preset.id}
                onClick={() => { setSelectedMoodId(selectedMoodId === preset.id ? "" : preset.id); setModePreview(null); setModeApplicationResult(null); setModeConfirming(false); }}
              >
                <strong>{preset.label}</strong><span>{preset.description}</span>
                <small>{preset.overrides.length ? `${preset.overrides.length} saved device customization${preset.overrides.length === 1 ? "" : "s"}` : "Suggested defaults"}</small>
              </button>)}
            </div>
          </section>

          <section className="panel mode-target-panel">
            <div className="mode-panel-heading"><div><p className="eyebrow">ROOM SCOPE</p><h3>Select one room</h3></div></div>
            <label className="mode-room-select">Room
              <select
                value={modeTargetRooms[0] ?? ""}
                onChange={(event) => {
                  setModeTargetRooms(event.target.value ? [event.target.value] : []);
                  setModePreview(null);
                  setModeConfirming(false);
                  setModeApplicationResult(null);
                }}
                disabled={modeApplyBusy || modePreviewBusy}
              >
                <option value="">Select a room</option>
                {rooms.map((room) => {
                  const name = getRoomLabel(room);
                  return name ? <option value={name} key={name}>{name}</option> : null;
                })}
              </select>
            </label>
            <div className="mode-target-list" aria-label="All devices included in this room">
              {targetModeDevices.map((device) => <div className="mode-target-option" key={getDeviceId(device)}>
                <span>{getDeviceName(device)} <small>{device.type} · {getDeviceId(device)}</small></span>
              </div>)}
              {modeTargetRooms.length === 1 && targetModeDevices.length === 0 &&
                <p className="fine-print">No devices are registered in this room. The mode can be reviewed, but there are no device actions.</p>}
            </div>
            <div className="mode-target-summary">
              Target: {modeTargetRooms[0] ?? "Select a room"} · all {targetModeDevices.length} registered device{targetModeDevices.length === 1 ? "" : "s"}
            </div>
          </section>

          <div className="mode-workflow-actions">
            <button
              className="primary-button"
              type="button"
              disabled={modePreviewBusy || !modeTargetRooms[0] || (!selectedSeasonId && !selectedMoodId)}
              onClick={generateModePreview}
            >{modePreviewBusy ? "Preparing preview…" : "Preview mode changes"}</button>
            {modePreview && <button className="secondary-button" type="button" onClick={cancelModePreview}>Cancel preview</button>}
            {modeHistory.some((item) => item.canRestore &&
              item.selection?.roomNames?.includes(modeTargetRooms[0])) && <button
              className="secondary-button"
              type="button"
              disabled={modeRestoring}
              onClick={() => setModeRestoring(true)}
            >{modeRestoring ? "Restoring…" : `Restore previous state${modeTargetRooms[0] ? ` · ${modeTargetRooms[0]}` : ""}`}</button>}
          </div>

          {modePreview && <section className="panel mode-preview-panel" aria-labelledby="mode-preview-heading">
            <div className="mode-panel-heading"><div><p className="eyebrow">REVIEW BEFORE APPLYING</p><h3 id="mode-preview-heading">Complete device action plan</h3><p className="section-subtitle">{[modePreview.seasonLabel, modePreview.moodLabel].filter(Boolean).join(" baseline + ")} · {modePreview.roomNames.join(", ")}</p></div></div>
            <div className="mode-preview-summary">
              <span>Proposed <strong>{modePreview.summary.proposed}</strong></span>
              <span>Skipped <strong>{modePreview.summary.skipped}</strong></span>
              <span>Already unchanged <strong>{modePreview.summary.unchanged}</strong></span>
            </div>
            {modePreview.summary.message && <p className="form-message">{modePreview.summary.message}</p>}
            {modePreview.actions.length > 0 && <div className="mode-action-list">
              {modePreview.actions.map((action, index) => <article className={`mode-action-row mode-action-${action.status}`} key={`${action.deviceId}-${action.property}-${index}`}>
                <div className="mode-action-heading">
                  <strong>{action.deviceName || action.deviceId || "Device"} <small>{action.roomName}</small></strong>
                  <span>{action.status.replace("_", " ")}</span>
                </div>
                {action.property === "policy"
                  ? <p><b>Device policy</b>: {action.value}</p>
                  : <p><b>{action.property === "setting" && action.deviceType === "Fan" ? "Fan speed" : action.property === "setting" ? "Temperature" : action.property}</b>: {formatModeActionValue(action, action.previousValue)} → {formatModeActionValue(action, action.value)}</p>}
                <small>{action.reason}{action.source ? ` · ${action.source === "mood" ? "Mood overlay" : action.source === "season" ? "Season baseline" : "Custom override"}` : ""}</small>
              </article>)}
            </div>}
            {modeConfirming ? <div className="mode-confirmation" role="alertdialog" aria-label="Confirm applying selected modes">
              <p>Confirm applying these simulated settings to the listed devices? This does not change saved profile preferences.</p>
              <button className="primary-button" type="button" disabled={modeApplyBusy || modePreview.summary.proposed === 0} onClick={confirmModeApplication}>{modeApplyBusy ? "Applying mode…" : "Apply Mode"}</button>
              <button className="secondary-button" type="button" disabled={modeApplyBusy} onClick={() => setModeConfirming(false)}>Keep preview</button>
            </div> : <button className="primary-button" type="button" disabled={modePreviewBusy || modePreview.summary.proposed === 0} onClick={() => setModeConfirming(true)}>Review confirmation</button>}
          </section>}

          {modeApplicationResult && <section className="panel mode-result-panel" aria-live="polite">
            <div className="mode-panel-heading"><div><p className="eyebrow">BACKEND-CONFIRMED RESULT</p><h3>Device action results</h3></div></div>
            <div className="mode-preview-summary">
              <span>Turned ON <strong>{modeResultCounts.turnedOn}</strong></span>
              <span>Turned OFF <strong>{modeResultCounts.turnedOff}</strong></span>
              <span>Settings changed <strong>{modeResultCounts.configured}</strong></span>
              <span>Unchanged <strong>{modeResultCounts.unchanged}</strong></span>
              <span>Failed <strong>{modeResultCounts.failed}</strong></span>
            </div>
            <div className="mode-action-list">
              {modeResultActions.map((action, index) => <article className={`mode-action-row mode-action-${action.status}`} key={`result-${action.deviceId}-${action.property}-${index}`}>
                <div className="mode-action-heading">
                  <strong>{action.deviceName || action.deviceId} <small>{action.roomName}</small></strong>
                  <span>{action.status.replace("_", " ")}</span>
                </div>
                <p>{action.property === "policy"
                  ? `Device policy: ${action.value}`
                  : `${action.property}: ${formatModeActionValue(action, action.previousValue)} → ${formatModeActionValue(action, action.value)}`}</p>
                {action.reason && <small>{action.reason}</small>}
              </article>)}
            </div>
          </section>}

          {modeRestoring && <div className="mode-confirmation mode-restore-confirmation" role="alertdialog" aria-label="Confirm restoring previous mode state">
            <p>Restore the device properties captured immediately before the latest unrestored mode application?</p>
            <button className="primary-button" type="button" onClick={restorePreviousMode}>Confirm restore</button>
            <button className="secondary-button" type="button" onClick={() => setModeRestoring(false)}>Cancel restore</button>
          </div>}

          <section className="panel mode-customize-panel">
            <div className="mode-panel-heading"><div><p className="eyebrow">DEVICE-SPECIFIC CUSTOMIZATION</p><h3>Adjust a suggested preset</h3><p className="section-subtitle">Saved overrides are specific to an existing device and property. The backend validates capability and value ranges.</p></div></div>
            <form className="mode-customize-form" onSubmit={saveModePresetOverrides}>
              <label>Preset type<select value={modePresetType} onChange={(event) => { setModePresetType(event.target.value); setModePresetId(""); setModeRuleValue(""); }}><option value="season">Season</option><option value="mood">Mood</option></select></label>
              <label>Preset<select value={modePresetId} onChange={(event) => { setModePresetId(event.target.value); setModePreview(null); }} required><option value="">Select a preset</option>{modePresetOptions.map((preset) => <option value={preset.id} key={preset.id}>{preset.label}</option>)}</select></label>
              <label>Device<select value={modeRuleDeviceId} onChange={(event) => { setModeRuleDeviceId(event.target.value); setModeRuleProperty(""); setModeRuleValue(""); }} required><option value="">Select a device</option>{devices.map((device) => <option value={getDeviceId(device)} key={getDeviceId(device)}>{getDeviceName(device)} · {getRoomName(device)}</option>)}</select></label>
              <label>Property<select value={modeRuleProperty} onChange={(event) => { setModeRuleProperty(event.target.value); setModeRuleValue(""); }} required disabled={!modeRuleDevice}><option value="">Select supported property</option>{modeRuleProperties.map((property) => <option value={property} key={property}>{property}</option>)}</select></label>
              {modeRuleProperty === "policy" ? <label>Device policy<select value={modeRuleValue} onChange={(event) => setModeRuleValue(event.target.value)} required><option value="">Choose policy</option><option value="ON">ON — power on and configure</option><option value="OFF">OFF — power off</option><option value="KEEP_CURRENT">KEEP_CURRENT — preserve state and settings</option><option value="CONFIGURE_ONLY">CONFIGURE_ONLY — change settings, preserve power</option></select></label>
                : modeRuleProperty === "color" ? <label>Value<input type="color" value={modeRuleValue || "#D96B78"} onChange={(event) => setModeRuleValue(event.target.value)} required /></label>
                  : <label>Value<input type="number" value={modeRuleValue} onChange={(event) => setModeRuleValue(event.target.value)} min={modeRuleProperty === "brightness" ? 0 : modeRuleProperty === "colorTemperature" ? 2700 : modeRuleDevice?.type === "AC" ? 16 : modeRuleDevice?.type === "Heater" ? 20 : 1} max={modeRuleProperty === "brightness" ? 100 : modeRuleProperty === "colorTemperature" ? 6500 : modeRuleDevice?.type === "AC" ? 30 : modeRuleDevice?.type === "Heater" ? 45 : 5} step={modeRuleProperty === "colorTemperature" ? 100 : 1} required disabled={!modeRuleProperty} /></label>}
              <button className="secondary-button" type="submit" disabled={modePresetBusy || !modePresetId || !modeRuleDeviceId || !modeRuleProperty}>{modePresetBusy ? "Saving…" : "Save device override"}</button>
            </form>
            {activeModePreset?.overrides?.length > 0 && <div className="mode-override-list">
              <h4>Saved overrides for {activeModePreset.label}</h4>
              {activeModePreset.overrides.map((rule) => {
                const device = devices.find((item) => getDeviceId(item) === rule.deviceId);
                return <div className="mode-override-row" key={`${rule.deviceId}-${rule.property}`}>
                  <span>{device ? getDeviceName(device) : rule.deviceId} · {rule.property} = {typeof rule.value === "boolean" ? (rule.value ? "ON" : "OFF") : rule.value}</span>
                  <button className="text-button" type="button" disabled={modePresetBusy} onClick={() => removeModePresetOverride(modePresetType, modePresetId, rule.deviceId, rule.property)}>Remove</button>
                </div>;
              })}
            </div>}
          </section>

          <section className="panel mode-history-panel">
            <div className="mode-panel-heading"><div><p className="eyebrow">PERSISTED AUDIT TRAIL</p><h3>Mode application history</h3></div></div>
            {modeHistory.length === 0 ? <p className="fine-print">No confirmed mode applications have been recorded.</p> : <div className="mode-history-list">
              {modeHistory.map((entry) => {
                const roomsAffected = [...new Set(entry.actions.map((action) => action.roomName).filter(Boolean))];
                const devicesAffected = [...new Set(entry.actions.map((action) => action.deviceName || action.deviceId).filter(Boolean))];
                const roomsInSelection = entry.selection?.roomNames ?? [];
                const devicesInSelection = entry.selection?.deviceIds ?? [];
                const season = seasonPresets.find((item) => item.id === entry.seasonId)?.label;
                const mood = moodPresets.find((item) => item.id === entry.moodId)?.label;
                return <article className="mode-history-entry" key={entry.id}>
                  <div className="mode-history-heading"><strong>{[season, mood].filter(Boolean).join(" + ") || "Mode application"}</strong><span className={`mode-outcome mode-outcome-${entry.outcome}`}>{entry.outcome}</span></div>
                  <p>{roomsAffected.length ? roomsAffected.join(", ") : roomsInSelection.length ? roomsInSelection.join(", ") : "All rooms"} · {devicesAffected.length ? devicesAffected.join(", ") : devicesInSelection.length ? devicesInSelection.join(", ") : "No compatible devices"}</p>
                  <small>{formatDate(entry.appliedAt)} · {entry.actions.filter((action) => action.status === "applied").length} applied · {entry.actions.filter((action) => action.status === "skipped").length} skipped · {entry.actions.filter((action) => action.status === "failed").length} failed</small>
                  {entry.restoredAt > 0 && <small className="mode-restored-label">Restored {formatDate(entry.restoredAt)}</small>}
                  {entry.actions.some((action) => action.status === "failed" || action.status === "skipped") && <details><summary>View per-device results</summary>{entry.actions.filter((action) => action.status === "failed" || action.status === "skipped").map((action, index) => <p key={`${action.deviceId}-${action.property}-${index}`}>{action.deviceName || action.deviceId} · {action.property}: {action.status} — {action.reason}</p>)}</details>}
                  {entry.restoreActions?.length > 0 && entry.restoredAt === 0 && <details><summary>View restore results</summary>{entry.restoreActions.map((action, index) => <p key={`${action.deviceId}-${action.property}-${index}`}>{action.deviceId} · {action.property}: {action.status}{action.reason ? ` — ${action.reason}` : ""}</p>)}</details>}
                </article>;
              })}
            </div>}
          </section>
        </section>}

        {activeSection === "energy" && <section id="energy" className="content-section">
          <div className="section-heading"><div><p className="eyebrow">ENERGY INTELLIGENCE</p><h2>Energy monitor</h2><p className="section-subtitle">Usage estimates from device power ratings and recorded run time.</p></div></div>
          {energyError && <div className="alert alert-error" role="alert">{energyError}</div>}
          {energySummaryError && <div className="alert alert-error" role="alert">{energySummaryError}</div>}
          <div className="stats-grid energy-stats">
            {stat("Recorded energy", `${totalEnergyKwh.toFixed(4)} kWh`, "Across recorded sessions", "ϟ", "amber")}
            {stat("Active sessions", activeSessionCount, "Devices currently consuming energy", "◷", "green")}
            {stat("Estimated bill", `₹${bill.total.toFixed(2)}`, "Using the configured tariff assumptions", "₹", "purple")}
            {stat("Live load", `${totalPowerWatts.toLocaleString("en-IN")} W`, "Combined rated power of ON devices", "⌁", "blue")}
          </div>
          {energySummaryLoading && !energySummary && <div className="panel">Loading energy summaries…</div>}
          {energySummary && (
            <div className="energy-analytics">
              <div className="energy-period-grid">
                {[
                  ["last24Hours", "Last 24 hours"],
                  ["last7Days", "Last 7 days"],
                  ["last30Days", "Last 30 days"],
                ].map(([key, label]) => {
                  const period = energySummary.windows[key];
                  const comparison = Number(period.previousEnergyKwh) > 0
                    ? `${Number(period.changeKwh) >= 0 ? "+" : ""}${Number(period.changeKwh).toFixed(5)} kWh vs previous period`
                    : "No previous-period energy available";

                  return (
                    <article className="panel energy-period-card" key={key}>
                      <span>{label} estimate</span>
                      <strong>{Number(period.energyKwh).toFixed(5)} kWh</strong>
                      <small>{comparison}</small>
                    </article>
                  );
                })}
              </div>
              <div className="energy-breakdown-grid">
                <section className="panel energy-breakdown">
                  <h3>Room usage · last 24 hours</h3>
                  {energySummary.windows.last24Hours.byRoom.length === 0
                    ? <p>No recorded consumption in this window.</p>
                    : energySummary.windows.last24Hours.byRoom.map((item) => (
                      <div className="bill-row" key={item.roomName}>
                        <span>{item.roomName}</span>
                        <strong>{Number(item.energyKwh).toFixed(5)} kWh</strong>
                      </div>
                    ))}
                </section>
                <section className="panel energy-breakdown">
                  <h3>Appliance usage · last 24 hours</h3>
                  {energySummary.windows.last24Hours.byAppliance.length === 0
                    ? <p>No recorded consumption in this window.</p>
                    : energySummary.windows.last24Hours.byAppliance.map((item) => (
                      <div className="bill-row" key={item.deviceId}>
                        <span>{item.deviceName}</span>
                        <strong>{Number(item.energyKwh).toFixed(5)} kWh</strong>
                      </div>
                    ))}
                </section>
              </div>
            </div>
          )}
          <div className="panel note-panel"><span>ⓘ</span><p>Energy usage is estimated from rated watts and the overlap of recorded ON sessions with each trailing time window. It is not a physical meter reading. Bill estimates use separate editable tariff assumptions.</p></div>
        </section>}

        {activeSection === "assistant" && <section id="assistant" className="content-section">
          <div className="section-heading"><div><p className="eyebrow">DATA-GROUNDED GUIDANCE</p><h2>Energy assistant</h2><p className="section-subtitle">Ask about energy history. Answers use backend-calculated session estimates and never control devices.</p></div></div>
          {assistantError && <div className="alert alert-error" role="alert"><div><p>{assistantError}</p><button className="secondary-button" type="button" onClick={() => askAssistant(assistantQuestion)} disabled={assistantBusy || !assistantQuestion.trim()}>Retry question</button></div><button type="button" onClick={() => setAssistantError("")} aria-label="Dismiss assistant error">×</button></div>}
          <div className="assistant-suggestions" aria-label="Suggested questions">
            {[
              "How much energy was estimated this week?",
              "Which appliance used the most energy in the last 24 hours?",
              "What could I review to reduce energy use?",
            ].map((question) => (
              <button className="secondary-button" key={question} type="button" disabled={assistantBusy} onClick={() => setAssistantQuestion(question)}>{question}</button>
            ))}
          </div>
          <div className="assistant-conversation panel" aria-live="polite">
            {assistantMessages.length === 0
              ? <p className="assistant-empty">Your conversation will appear here. The backend uses estimated energy-session data; it does not provide meter readings or appliance automation.</p>
              : assistantMessages.map((message, index) => (
                <article className="assistant-message" key={`${index}-${message.question}`}>
                  <div className="assistant-question"><span>You</span><p>{message.question}</p></div>
                  <div className="assistant-answer">
                    <div className="assistant-answer-heading"><strong>SmartHomeOS</strong><span>{message.source === "cloud" ? "AI explanation" : "Rule-based fallback"}</span></div>
                    {message.notice && <p className="assistant-notice" role="status">{message.notice}</p>}
                    <p>{message.answer}</p>
                    <div className="assistant-metrics">
                      {[
                        ["last24Hours", "24 hours"],
                        ["last7Days", "7 days"],
                        ["last30Days", "30 days"],
                      ].map(([period, label]) => (
                        <div className="panel assistant-metric" key={period}>
                          <span>{label} · estimated</span>
                          <strong>{Number(message.windows[period].energyKwh).toFixed(5)} kWh</strong>
                          <small>{Number(message.windows[period].previousEnergyKwh) > 0
                            ? `${Number(message.windows[period].changeKwh) >= 0 ? "+" : ""}${Number(message.windows[period].changeKwh).toFixed(5)} kWh vs previous window`
                            : "No previous-window energy available"}</small>
                        </div>
                      ))}
                    </div>
                    {message.recommendations?.map((recommendation) => (
                      <div className="panel assistant-recommendation" key={recommendation.title}>
                        <strong>{recommendation.title}</strong>
                        <p>{recommendation.explanation}</p>
                        <small>Evidence: {recommendation.observation}</small>
                        <small>Suggested action: {recommendation.recommendedAction}</small>
                        <small>Benefit is not quantified. {recommendation.assumptions}</small>
                      </div>
                    ))}
                    {message.limitations?.length > 0 && <ul className="assistant-limitations">{message.limitations.map((limitation) => <li key={limitation}>{limitation}</li>)}</ul>}
                  </div>
                </article>
              ))}
          </div>
          <form className="assistant-form panel" onSubmit={sendAssistantQuestion}>
            <label htmlFor="assistant-question">Your question</label>
            <textarea id="assistant-question" value={assistantQuestion} onChange={(event) => setAssistantQuestion(event.target.value)} maxLength={1000} rows={3} placeholder="Ask about your estimated energy usage…" />
            <div className="assistant-form-footer"><small>{assistantQuestion.length}/1000 characters · Request timeout: 20 seconds</small><button className="primary-button" type="submit" disabled={assistantBusy || !assistantQuestion.trim()}>{assistantBusy ? "Calculating…" : "Ask assistant"}</button></div>
          </form>
          <div className="panel note-panel"><span>ⓘ</span><p>Energy figures come from the C++ backend and are estimates based on rated watts and recorded ON-session duration. Cloud AI is optional and configured only on the backend. If it is not configured or available, a deterministic summary is shown. Suggestions are advisory; no model response can operate a device.</p></div>
        </section>}

        {activeSection === "camera" && <section id="camera" className="content-section">
          <div className="section-heading"><div><p className="eyebrow">LOCAL AND CONSENT-BASED</p><h2>Facial recognition setup</h2><p className="section-subtitle">Enroll consenting household members against existing SmartHomeOS profiles. Recognition starts only for rooms with explicit camera opt-in and global automation enabled.</p></div><span className={`camera-status camera-status-${automationStatus}`} role="status"><i />FACE SERVICE {automationStatus.toUpperCase()}</span></div>
          {automationError && <div className="alert alert-error" role="alert">{automationError}</div>}
          {enrollmentMessage && <div className={`form-message ${enrollmentMessage.startsWith("Enrollment failed") || enrollmentMessage.startsWith("Could not") ? "is-error" : "is-success"}`} role="status">{enrollmentMessage}</div>}
          <div className="camera-layout">
            <section className="panel camera-panel">
              <div className="global-automation-toggle">
                <span><strong>Global camera automation</strong><small>{automationEnabled ? "Enabled · still requires a room-specific manual start and apply confirmation" : "Disabled by default; enable only when you want to use camera automation"}</small></span>
                <button type="button" role="switch" aria-checked={automationEnabled} aria-label="Enable global camera automation" className={`toggle ${automationEnabled ? "toggle-on" : ""}`} disabled={automationBusy || Boolean(automationSession)} onClick={() => updateAutomationEnabled(!automationEnabled)}><span /></button>
              </div>
              {cameraStream && !automationSession
                ? <video className="camera-preview" ref={cameraVideoRef} autoPlay muted playsInline aria-label="Temporary local enrollment camera preview" />
                : <div className="camera-preview camera-preview-placeholder"><span>◉</span><strong>Enrollment preview is off</strong><small>Press Enroll member to request camera access and capture local samples.</small></div>}
              <form className="device-form enrollment-form" onSubmit={enrollHouseholdMember}>
                <div className="form-heading"><div><p className="eyebrow">EXPLICIT ENROLLMENT</p><h3>Link a face to an existing profile</h3></div></div>
                <label>SmartHomeOS profile
                  <select value={enrollmentProfileId} onChange={(event) => setEnrollmentProfileId(event.target.value)} required disabled={enrollmentBusy || Boolean(automationSession)}>
                    <option value="">Select an existing profile</option>
                    {profiles.map((profile) => <option value={profile.id} key={profile.id}>{profile.displayName}</option>)}
                  </select>
                </label>
                <label className="checkbox-label"><input type="checkbox" checked={enrollmentConsent} onChange={(event) => setEnrollmentConsent(event.target.checked)} disabled={enrollmentBusy} required /> The person being enrolled has explicitly consented to local biometric face-template storage and matching.</label>
                <button className="primary-button" type="submit" disabled={enrollmentBusy || !enrollmentConsent || !enrollmentProfileId || !profiles.length || Boolean(automationSession) || automationStatus !== "online"}>{enrollmentBusy ? "Capturing local samples…" : "Enroll consenting member"}</button>
              </form>
              <div className="enrollment-list">
                <h3>Enrolled profiles</h3>
                {enrollments.length === 0
                  ? <p>No face enrollments are registered.</p>
                  : enrollments.map((item) => <div className="enrollment-row" key={item.id}><span><strong>{item.profileName}</strong><small>Profile {item.profileId} · added {formatDate(item.createdAt)}</small></span><button type="button" className="secondary-button danger-button" disabled={Boolean(automationSession) || deletingEnrollment === item.id || automationStatus !== "online"} onClick={() => removeEnrollment(item)}>{deletingEnrollment === item.id ? "Deleting…" : "Delete enrollment"}</button></div>)}
              </div>
            </section>
            <aside className="panel camera-privacy">
              <h3>Local processing and limitations</h3>
              <ul>
                <li>OpenCV Haar face detection and LBPH matching run on this computer; raw frames are sent only to 127.0.0.1:8765 and are never persisted or logged.</li>
                <li>Face crops are stored encrypted with the current Windows account's DPAPI protection in %LOCALAPPDATA%\SmartHomeOS.</li>
                <li>Matching needs 3 consistent observations; the configurable distance threshold defaults to 55 (lower distances match).</li>
                <li>Multiple detected faces are ambiguous; no person is arbitrarily selected and no preferences are applied.</li>
                <li>No face is not evidence of an empty room. Camera failure is reported as Camera Offline.</li>
                <li>Face matching is probabilistic and is not liveness detection, secure authentication, or a safety mechanism.</li>
              </ul>
              <p className="fine-print">Manual Control remains available without this Python service. Always obtain consent from each enrolled person and delete their enrollment when requested.</p>
            </aside>
          </div>
          <section className="panel automation-event-list">
            <h3>Recent automation events</h3>
            {automationEvents.length === 0 ? <p>No confirmed automation applications have been recorded.</p> : automationEvents.map((item) => <div className="enrollment-row" key={item.id}><span><strong>{item.outcome} · {item.roomName}</strong><small>Profile {item.profileId || "—"} · {formatDate(item.eventTime)} · {item.settingsChanged.length} settings recorded</small></span></div>)}
          </section>
        </section>}

        {activeSection === "billing" && <section id="billing" className="content-section">
          <div className="section-heading"><div><p className="eyebrow">COST PLANNING</p><h2>Electricity bill estimator</h2><p className="section-subtitle">An editable, illustrative slab-based estimate — not an official utility bill.</p></div><button className="secondary-button" onClick={() => setShowTariffSettings((v) => !v)}>{showTariffSettings ? "Hide tariff settings" : "⚙ Edit tariff"}</button></div>
          {showTariffSettings && <form className="panel tariff-form" onSubmit={saveTariffSettings}><div className="form-grid">{[["slab1", "First 100 units (₹/unit)"], ["slab2", "101–300 units (₹/unit)"], ["slab3", "301–500 units (₹/unit)"], ["slab4", "Above 500 units (₹/unit)"], ["wheeling", "Wheeling charge (₹/unit)"], ["fixedCharge", "Fixed charge (₹/period)"], ["dutyPercent", "Electricity duty (%)"]].map(([key, label]) => <label key={key}>{label}<input type="number" min="0" step="0.01" value={tariffInputs[key]} onChange={(e) => handleTariffInput(key, e.target.value)} required /></label>)}</div><div className="form-actions"><button type="button" className="secondary-button" onClick={resetTariff}>Reset defaults</button><button className="primary-button">Apply rates</button></div><p className="fine-print">Tariff values are held in React state and reset on page reload. Verify the assumptions for your consumer category.</p></form>}
          <div className="billing-layout"><div className="bill-total-card"><p>ESTIMATED TOTAL</p><strong>₹{bill.total.toFixed(2)}</strong><span>Based on {bill.units.toFixed(4)} recorded units</span><div className="bill-total-foot"><span>Energy charges</span><strong>₹{bill.energyCharge.toFixed(2)}</strong></div></div><div className="panel bill-breakdown"><h3>Charge breakdown</h3>{[["First 100 units", bill.first * tariff.slab1], ["101–300 units", bill.second * tariff.slab2], ["301–500 units", bill.third * tariff.slab3], ["Above 500 units", bill.fourth * tariff.slab4], ["Wheeling charges", bill.wheelingCharge], ["Fixed charge", bill.fixedCharge], ["Electricity duty", bill.duty]].map(([label, value]) => <div className="bill-row" key={label}><span>{label}</span><strong>₹{Number(value).toFixed(2)}</strong></div>)}<div className="bill-row bill-row-total"><span>Estimated total</span><strong>₹{bill.total.toFixed(2)}</strong></div></div></div>
          <p className="fine-print">The default slab rates, fixed charge and duty are assumptions for demonstration. This estimate does not apply a calendar billing-period filter and may omit charges or adjustments on an actual bill.</p>
        </section>}

        {activeSection === "history" && <section id="history" className="content-section">
          <div className="section-heading"><div><p className="eyebrow">ACTIVITY LOG</p><h2>Energy session history</h2><p className="section-subtitle">A record of device activity reported by the backend.</p></div><span className="count-chip">{sessions.length} records</span></div>
          {energyLoading ? <div className="panel">Loading energy history…</div> : sessions.length === 0 ? <div className="empty-state"><span>◷</span><h3>No sessions yet</h3><p>Turn on a device to begin tracking its estimated energy use.</p></div> : <div className="panel table-panel"><div className="table-wrap"><table><thead><tr><th>Device</th><th>Room</th><th>Power</th><th>Duration</th><th>Energy</th><th>Status</th><th>Started</th></tr></thead><tbody>{[...sessions].sort((a, b) => Number(b.startedAt || 0) - Number(a.startedAt || 0)).map((session, index) => { const active = session.active || Number(session.endedAt) === 0; const duration = active ? Math.max(0, nowSeconds - Number(session.startedAt || 0)) : Math.max(0, Number(session.endedAt || 0) - Number(session.startedAt || 0)); return <tr key={session.id ?? `${session.deviceId}-${index}`}><td><strong>{session.deviceName ?? "Unknown device"}</strong></td><td>{session.roomName ?? "—"}</td><td>{Number(session.powerWatts || 0)} W</td><td>{formatDuration(duration)}</td><td>{Number(session.energyKwh || 0).toFixed(5)} kWh</td><td><span className={`status-tag ${active ? "status-on" : "status-off"}`}>{active ? "ACTIVE" : "COMPLETED"}</span></td><td>{formatDate(Number(session.startedAt || 0))}</td></tr>; })}</tbody></table></div></div>}
        </section>}

        <footer className="footer"><span>SmartHomeOS <span className="footer-dot">●</span> Built for smarter everyday living</span><span>Energy readings are software estimates · Local API</span></footer>
      </main>
    </div>
  );
}

export default App;
