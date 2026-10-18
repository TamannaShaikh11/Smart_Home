import base64
import binascii
import ctypes
import json
import math
import os
import re
import threading
import time
from ctypes import wintypes
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse

try:
    import cv2
    import numpy as np
except ImportError as error:
    raise SystemExit(
        "Face automation requires OpenCV contrib. Install "
        "requirements-face.txt with the documented command."
    ) from error


HOST = "127.0.0.1"
PORT = int(os.environ.get("SMARTHOME_FACE_PORT", "8765"))
MATCH_THRESHOLD = float(os.environ.get("SMARTHOME_FACE_MATCH_THRESHOLD", "55"))
CONSISTENT_MATCHES = 3
FACE_SIZE = (128, 128)
FACE_BRIGHTNESS_RANGE = (35.0, 220.0)
FACE_CONTRAST_MINIMUM = 15.0
FACE_SHARPNESS_MINIMUM = 8.0
MAX_BODY_BYTES = 4 * 1024 * 1024
ENTROPY = b"SmartHomeOS local face templates v1"
STORE_PATH = (
    Path(os.environ.get("LOCALAPPDATA", str(Path.home())))
    / "SmartHomeOS"
    / "face_templates.dpapi"
)

if not math.isfinite(MATCH_THRESHOLD) or MATCH_THRESHOLD <= 0:
    raise SystemExit("SMARTHOME_FACE_MATCH_THRESHOLD must be a finite positive number.")


class DataBlob(ctypes.Structure):
    _fields_ = [
        ("cbData", wintypes.DWORD),
        ("pbData", ctypes.POINTER(ctypes.c_ubyte)),
    ]


def is_allowed_origin(origin):
    try:
        parsed = urlparse(origin)
        return (
            parsed.scheme == "http"
            and parsed.hostname in {"localhost", "127.0.0.1"}
            and parsed.port is not None
            and 5173 <= parsed.port <= 5199
            and not parsed.path
            and not parsed.query
            and not parsed.fragment
        )
    except ValueError:
        return False


def _blob(data):
    buffer = (ctypes.c_ubyte * len(data)).from_buffer_copy(data)
    return DataBlob(len(data), ctypes.cast(buffer, ctypes.POINTER(ctypes.c_ubyte))), buffer


def _protect(data, decrypt=False):
    if os.name != "nt":
        raise RuntimeError("Encrypted face enrollment storage requires Windows DPAPI.")
    crypt32 = ctypes.windll.crypt32
    kernel32 = ctypes.windll.kernel32
    protect = crypt32.CryptProtectData
    protect.argtypes = [
        ctypes.POINTER(DataBlob),
        wintypes.LPCWSTR,
        ctypes.POINTER(DataBlob),
        ctypes.c_void_p,
        ctypes.c_void_p,
        wintypes.DWORD,
        ctypes.POINTER(DataBlob),
    ]
    protect.restype = wintypes.BOOL
    unprotect = crypt32.CryptUnprotectData
    unprotect.argtypes = [
        ctypes.POINTER(DataBlob),
        ctypes.POINTER(wintypes.LPWSTR),
        ctypes.POINTER(DataBlob),
        ctypes.c_void_p,
        ctypes.c_void_p,
        wintypes.DWORD,
        ctypes.POINTER(DataBlob),
    ]
    unprotect.restype = wintypes.BOOL
    kernel32.LocalFree.argtypes = [ctypes.c_void_p]
    kernel32.LocalFree.restype = ctypes.c_void_p
    source, source_buffer = _blob(data)
    entropy, entropy_buffer = _blob(ENTROPY)
    destination = DataBlob()
    if decrypt:
        description = wintypes.LPWSTR()
        succeeded = unprotect(
            ctypes.byref(source),
            ctypes.byref(description),
            ctypes.byref(entropy),
            None,
            None,
            0,
            ctypes.byref(destination),
        )
    else:
        succeeded = protect(
            ctypes.byref(source),
            "SmartHomeOS face templates",
            ctypes.byref(entropy),
            None,
            None,
            0,
            ctypes.byref(destination),
        )
    del source_buffer, entropy_buffer
    if not succeeded:
        raise OSError("Windows DPAPI could not protect or unlock face enrollment data.")
    try:
        return ctypes.string_at(destination.pbData, destination.cbData)
    finally:
        kernel32.LocalFree(destination.pbData)


class FaceMatcher:
    def __init__(self):
        cascade_path = Path(cv2.data.haarcascades) / "haarcascade_frontalface_default.xml"
        self.detector = cv2.CascadeClassifier(str(cascade_path))
        if self.detector.empty():
            raise RuntimeError("OpenCV face detection model could not be loaded.")
        self.lock = threading.RLock()
        self.enrollments = {}
        self.sessions = {}
        self.active_session = None
        self._retrain()
        STORE_PATH.parent.mkdir(parents=True, exist_ok=True)
        self._load()

    def _load(self):
        if not STORE_PATH.exists():
            print(
                "[face-service] No encrypted face-template store found; "
                "no enrolled identities are available.",
                flush=True,
            )
            return
        try:
            cleartext = _protect(STORE_PATH.read_bytes(), decrypt=True)
            stored = json.loads(cleartext.decode("utf-8"))
            if not isinstance(stored, dict):
                raise ValueError("Invalid enrollment data.")
            for key, entry in stored.items():
                if not re.fullmatch(r"[A-Za-z0-9_-]{8,80}", key):
                    raise ValueError("Stored enrollment ID is invalid.")
                if (
                    not isinstance(entry, dict)
                    or not isinstance(entry.get("displayName"), str)
                    or not isinstance(entry.get("samples"), list)
                    or not 8 <= len(entry["samples"]) <= 20
                ):
                    raise ValueError("Stored enrollment metadata is invalid.")
                samples = []
                for item in entry["samples"]:
                    if not isinstance(item, str):
                        raise ValueError("Stored face sample is invalid.")
                    sample_bytes = base64.b64decode(item, validate=True)
                    if len(sample_bytes) != FACE_SIZE[0] * FACE_SIZE[1]:
                        raise ValueError("Stored face sample dimensions are invalid.")
                    samples.append(
                        np.frombuffer(sample_bytes, dtype=np.uint8).reshape(FACE_SIZE)
                    )
                self.enrollments[key] = {
                    "displayName": entry["displayName"],
                    "samples": samples,
                }
        except (OSError, ValueError, UnicodeError, json.JSONDecodeError) as error:
            raise RuntimeError("Encrypted face enrollments could not be read.") from error
        self._retrain()
        print(
            f"[face-service] Loaded {len(self.enrollments)} encrypted "
            "face enrollment(s).",
            flush=True,
        )

    def _save(self, enrollments=None):
        enrollments = self.enrollments if enrollments is None else enrollments
        stored = {
            key: {
                "displayName": entry["displayName"],
                "samples": [
                    base64.b64encode(sample.tobytes()).decode("ascii")
                    for sample in entry["samples"]
                ],
            }
            for key, entry in enrollments.items()
        }
        protected = _protect(
            json.dumps(stored, separators=(",", ":")).encode("utf-8")
        )
        temporary = STORE_PATH.with_suffix(".tmp")
        with temporary.open("wb") as output:
            output.write(protected)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, STORE_PATH)

    @staticmethod
    def _train(enrollments):
        recognizer = cv2.face.LBPHFaceRecognizer_create(
            radius=1, neighbors=8, grid_x=8, grid_y=8
        )
        label_ids = {}
        samples = []
        labels = []
        for label, (enrollment_id, entry) in enumerate(enrollments.items(), start=1):
            label_ids[label] = enrollment_id
            samples.extend(entry["samples"])
            labels.extend([label] * len(entry["samples"]))
        trained = bool(samples)
        if trained:
            recognizer.train(samples, np.asarray(labels, dtype=np.int32))
        return recognizer, label_ids, trained

    def _retrain(self):
        self.recognizer, self.label_ids, self.trained = self._train(
            self.enrollments
        )

    def _decode_frame(self, encoded):
        if not isinstance(encoded, str) or len(encoded) > MAX_BODY_BYTES:
            raise ValueError("A valid local camera frame is required.")
        if encoded.startswith("data:"):
            encoded = encoded.split(",", 1)[-1]
        try:
            raw = base64.b64decode(encoded, validate=True)
        except (ValueError, binascii.Error) as error:
            raise ValueError("Camera frame must be base64-encoded JPEG data.") from error
        image = cv2.imdecode(np.frombuffer(raw, dtype=np.uint8), cv2.IMREAD_COLOR)
        if image is None:
            raise ValueError("Camera frame is not a readable image.")
        return image

    def _faces(self, image):
        gray = cv2.cvtColor(image, cv2.COLOR_BGR2GRAY)
        boxes = self.detector.detectMultiScale(
            gray, scaleFactor=1.1, minNeighbors=5, minSize=(70, 70)
        )
        result = []
        for x, y, width, height in boxes:
            face = gray[y : y + height, x : x + width]
            result.append(cv2.resize(face, FACE_SIZE, interpolation=cv2.INTER_AREA))
        return result

    @staticmethod
    def _is_good_quality(face):
        brightness = float(np.mean(face))
        contrast = float(np.std(face))
        sharpness = float(cv2.Laplacian(face, cv2.CV_64F).var())
        return (
            FACE_BRIGHTNESS_RANGE[0] <= brightness <= FACE_BRIGHTNESS_RANGE[1]
            and contrast >= FACE_CONTRAST_MINIMUM
            and sharpness >= FACE_SHARPNESS_MINIMUM
        )

    def enroll(self, enrollment_id, display_name, frames):
        if not re.fullmatch(r"[A-Za-z0-9_-]{8,80}", enrollment_id):
            raise ValueError("Enrollment ID is invalid.")
        if not isinstance(display_name, str) or not display_name.strip() or len(display_name) > 80:
            raise ValueError("A display name between 1 and 80 characters is required.")
        if not isinstance(frames, list) or not 8 <= len(frames) <= 20:
            raise ValueError("Capture 8 to 20 consented face samples.")
        with self.lock:
            if enrollment_id in self.enrollments:
                raise RuntimeError(
                    "This enrollment already exists. Delete it before re-enrolling."
                )
        faces = []
        for frame in frames:
            detections = self._faces(self._decode_frame(frame))
            if not detections:
                raise ValueError(
                    "No face was detected in an enrollment frame. Center the "
                    "consenting person's face in the preview, improve lighting, "
                    "and retry."
                )
            if len(detections) > 1:
                raise ValueError(
                    "Multiple faces were detected in an enrollment frame. "
                    "Ensure only the consenting person is visible, then retry."
                )
            if not self._is_good_quality(detections[0]):
                raise ValueError(
                    "Enrollment frame is too dark, washed out, low contrast, or blurry."
                )
            faces.append(detections[0])
        with self.lock:
            updated_enrollments = dict(self.enrollments)
            updated_enrollments[enrollment_id] = {
                "displayName": display_name.strip(),
                "samples": faces,
            }
            recognizer, label_ids, trained = self._train(updated_enrollments)
            self._save(updated_enrollments)
            self.enrollments = updated_enrollments
            self.recognizer = recognizer
            self.label_ids = label_ids
            self.trained = trained
            print(
                f"[face-service] Enrollment stored and recognition model "
                f"rebuilt; {len(faces)} sample(s), "
                f"{len(self.enrollments)} enrollment(s).",
                flush=True,
            )
        return len(faces)

    def delete_enrollment(self, enrollment_id):
        with self.lock:
            if enrollment_id not in self.enrollments:
                return False
            updated_enrollments = dict(self.enrollments)
            del updated_enrollments[enrollment_id]
            recognizer, label_ids, trained = self._train(updated_enrollments)
            self._save(updated_enrollments)
            self.enrollments = updated_enrollments
            self.recognizer = recognizer
            self.label_ids = label_ids
            self.trained = trained
            for session in self.sessions.values():
                session["candidate"] = None
                session["count"] = 0
                session["resolved"] = None
            return True

    def start_session(self, session_id, room_name):
        if (
            not isinstance(session_id, str)
            or not re.fullmatch(r"[A-Za-z0-9_-]{8,100}", session_id)
        ):
            raise ValueError("sessionId is invalid.")
        if not isinstance(room_name, str) or not room_name.strip() or len(room_name) > 100:
            raise ValueError("A valid roomName is required.")
        with self.lock:
            if session_id in self.sessions:
                raise RuntimeError("Recognition session ID is already active.")
            if self.active_session is not None:
                active = self.sessions.get(self.active_session)
                if active is not None and time.time() - active["lastSeen"] <= 20:
                    raise RuntimeError("A camera recognition session is already active.")
                self.sessions.pop(self.active_session, None)
                self.active_session = None
            self.sessions[session_id] = {
                "roomName": room_name,
                "candidate": None,
                "count": 0,
                "unknownCount": 0,
                "resolved": None,
                "lastSeen": int(time.time()),
            }
            self.active_session = session_id

    def stop_session(self, session_id):
        with self.lock:
            if session_id not in self.sessions:
                return False
            del self.sessions[session_id]
            if self.active_session == session_id:
                self.active_session = None
            return True

    def recognize(self, session_id, room_name, encoded_frame):
        image = self._decode_frame(encoded_frame)
        faces = self._faces(image)
        with self.lock:
            session = self.sessions.get(session_id)
            if session is None or self.active_session != session_id:
                raise KeyError("Recognition session is not active.")
            if session["roomName"] != room_name:
                raise PermissionError("Frame room does not match its recognition session.")
            session["lastSeen"] = int(time.time())
            if len(faces) > 1:
                session["candidate"] = None
                session["count"] = 0
                session["unknownCount"] = 0
                session["resolved"] = None
                return {"status": "ambiguous", "faceCount": len(faces)}
            if not faces:
                session["candidate"] = None
                session["count"] = 0
                session["unknownCount"] = 0
                session["resolved"] = None
                return {"status": "no_person", "faceCount": 0}
            if not self._is_good_quality(faces[0]):
                session["candidate"] = None
                session["count"] = 0
                session["unknownCount"] = 0
                session["resolved"] = None
                return {
                    "status": "uncertain",
                    "faceCount": 1,
                    "reason": "poor_quality",
                }
            if not self.trained:
                session["candidate"] = None
                session["count"] = 0
                session["resolved"] = None
                session["unknownCount"] += 1
                status = (
                    "unknown"
                    if session["unknownCount"] >= CONSISTENT_MATCHES
                    else "uncertain"
                )
                return {
                    "status": status,
                    "faceCount": 1,
                    "observations": session["unknownCount"],
                    "requiredObservations": CONSISTENT_MATCHES,
                }
            label, distance = self.recognizer.predict(faces[0])
            enrollment_id = self.label_ids.get(label)
            if enrollment_id is None or distance > MATCH_THRESHOLD:
                session["candidate"] = None
                session["count"] = 0
                session["resolved"] = None
                session["unknownCount"] += 1
                status = (
                    "unknown"
                    if session["unknownCount"] >= CONSISTENT_MATCHES
                    else "uncertain"
                )
                return {
                    "status": status,
                    "faceCount": 1,
                    "observations": session["unknownCount"],
                    "requiredObservations": CONSISTENT_MATCHES,
                }
            session["unknownCount"] = 0
            if session["candidate"] == enrollment_id:
                session["count"] += 1
            else:
                session["candidate"] = enrollment_id
                session["count"] = 1
                session["resolved"] = None
            entry = self.enrollments[enrollment_id]
            if session["count"] < CONSISTENT_MATCHES:
                return {
                    "status": "pending",
                    "faceCount": 1,
                    "observations": session["count"],
                    "requiredObservations": CONSISTENT_MATCHES,
                    "displayName": entry["displayName"],
                }
            session["resolved"] = enrollment_id
            return {
                "status": "matched",
                "faceCount": 1,
                "observations": session["count"],
                "enrollmentId": enrollment_id,
                "displayName": entry["displayName"],
            }


MATCHER = FaceMatcher()


class Handler(BaseHTTPRequestHandler):
    server_version = "SmartHomeFaceLocal/1.0"

    def log_message(self, _format, *_args):
        return

    def _send(self, status, payload):
        body = json.dumps(payload, separators=(",", ":")).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        origin = self.headers.get("Origin", "")
        if is_allowed_origin(origin):
            self.send_header("Access-Control-Allow-Origin", origin)
            self.send_header("Vary", "Origin")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def _body(self):
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError as error:
            raise ValueError("Invalid request length.") from error
        if length <= 0 or length > MAX_BODY_BYTES:
            raise ValueError("Request body must be between 1 byte and 4 MiB.")
        try:
            return json.loads(self.rfile.read(length).decode("utf-8"))
        except (UnicodeError, json.JSONDecodeError) as error:
            raise ValueError("Request body must be valid JSON.") from error

    def do_OPTIONS(self):
        origin = self.headers.get("Origin", "")
        if not is_allowed_origin(origin):
            self._send(403, {"error": "Origin is not allowed."})
            return
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", origin)
        self.send_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.send_header("Access-Control-Max-Age", "600")
        self.end_headers()

    def do_GET(self):
        path = urlparse(self.path).path
        if path == "/health":
            with MATCHER.lock:
                model_ready = (
                    not MATCHER.detector.empty()
                    and hasattr(MATCHER, "recognizer")
                )
                self._send(200, {
                    "status": "online",
                    "model": "OpenCV Haar cascade + LBPH",
                    "modelReady": model_ready,
                    "recognitionReady": model_ready and MATCHER.trained,
                    "enrollmentCount": len(MATCHER.enrollments),
                    "templateStorePresent": STORE_PATH.is_file(),
                    "threshold": MATCH_THRESHOLD,
                    "consistentObservations": CONSISTENT_MATCHES,
                    "activeSession": MATCHER.active_session is not None,
                })
            return
        if path.startswith("/recognition/sessions/"):
            session_id = unquote(path.rsplit("/", 1)[-1])
            with MATCHER.lock:
                session = MATCHER.sessions.get(session_id)
                if session is None or MATCHER.active_session != session_id:
                    self._send(404, {"error": "Recognition session is not active."})
                    return
                self._send(200, {
                    "sessionId": session_id,
                    "roomName": session["roomName"],
                    "status": "active",
                })
            return
        if path == "/enrollments":
            with MATCHER.lock:
                self._send(200, [
                    {
                        "id": enrollment_id,
                        "displayName": entry["displayName"],
                        "sampleCount": len(entry["samples"]),
                    }
                    for enrollment_id, entry in MATCHER.enrollments.items()
                ])
            return
        self._send(404, {"error": "Endpoint not found."})

    def do_POST(self):
        path = urlparse(self.path).path
        try:
            body = self._body()
            if path.startswith("/enrollments/") and path.endswith("/train"):
                enrollment_id = unquote(path.split("/")[2])
                frames = body.get("frames")
                print(
                    "[face-service] Received local enrollment request "
                    f"({len(frames) if isinstance(frames, list) else 0} sample(s)).",
                    flush=True,
                )
                sample_count = MATCHER.enroll(
                    enrollment_id,
                    body.get("displayName"),
                    frames,
                )
                with MATCHER.lock:
                    self._send(201, {
                        "enrollmentId": enrollment_id,
                        "samples": sample_count,
                        "enrollmentCount": len(MATCHER.enrollments),
                        "recognitionReady": MATCHER.trained,
                    })
                return
            if path == "/recognition/sessions":
                session_id = body.get("sessionId")
                room_name = body.get("roomName")
                if not isinstance(session_id, str) or not isinstance(room_name, str) or not room_name:
                    raise ValueError("sessionId and roomName are required.")
                MATCHER.start_session(session_id, room_name)
                self._send(201, {"sessionId": session_id, "roomName": room_name, "status": "active"})
                return
            if path.startswith("/recognition/sessions/") and path.endswith("/frame"):
                parts = path.strip("/").split("/")
                if len(parts) != 4:
                    self._send(404, {"error": "Endpoint not found."})
                    return
                session_id = unquote(parts[2])
                room_name = body.get("roomName")
                result = MATCHER.recognize(
                    session_id, room_name, body.get("frame")
                )
                self._send(200, result)
                return
            self._send(404, {"error": "Endpoint not found."})
        except KeyError as error:
            self._send(404, {"error": str(error).strip("'")})
        except PermissionError as error:
            self._send(409, {"error": str(error)})
        except RuntimeError as error:
            if path.startswith("/enrollments/") and path.endswith("/train"):
                print(
                    f"[face-service] Enrollment request failed: {error}",
                    flush=True,
                )
            self._send(409, {"error": str(error)})
        except (ValueError, TypeError) as error:
            if path.startswith("/enrollments/") and path.endswith("/train"):
                print(
                    f"[face-service] Enrollment request failed: {error}",
                    flush=True,
                )
            self._send(400, {"error": str(error)})
        except OSError as error:
            if path.startswith("/enrollments/") and path.endswith("/train"):
                print(
                    "[face-service] Enrollment storage failed "
                    f"({type(error).__name__}).",
                    flush=True,
                )
            self._send(500, {"error": "Local encrypted enrollment storage failed."})

    def do_DELETE(self):
        path = urlparse(self.path).path
        if path.startswith("/recognition/sessions/"):
            session_id = unquote(path.rsplit("/", 1)[-1])
            if MATCHER.stop_session(session_id):
                self._send(200, {"sessionId": session_id, "status": "stopped"})
            else:
                self._send(404, {"error": "Recognition session is not active."})
            return
        if path.startswith("/enrollments/"):
            enrollment_id = unquote(path.rsplit("/", 1)[-1])
            if MATCHER.delete_enrollment(enrollment_id):
                self._send(200, {"enrollmentId": enrollment_id, "deleted": True})
            else:
                self._send(404, {"error": "Enrollment was not found."})
            return
        self._send(404, {"error": "Endpoint not found."})


def main():
    if os.name != "nt":
        raise SystemExit("Face automation is supported only on Windows in this build.")
    server = ThreadingHTTPServer((HOST, PORT), Handler)
    server.daemon_threads = True
    print(
        f"SmartHomeOS local face service listening at http://{HOST}:{PORT}; "
        "templates are protected with Windows DPAPI."
    )
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
