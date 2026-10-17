import base64
import os
import socket
import tempfile
import threading
import unittest
from http.server import ThreadingHTTPServer
from pathlib import Path
from unittest.mock import Mock, patch
from urllib.error import HTTPError
from urllib.request import Request, urlopen

import cv2
import numpy as np


ROOT = Path(__file__).resolve().parent
TEST_DATA = tempfile.TemporaryDirectory(prefix="smarthome-face-test-")
os.environ["LOCALAPPDATA"] = TEST_DATA.name

import face_automation_service as face_service


class FaceAutomationServiceTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(
            ("127.0.0.1", 0), face_service.Handler
        )
        cls.server.daemon_threads = True
        cls.port = cls.server.server_address[1]
        cls.base_url = f"http://127.0.0.1:{cls.port}"
        cls.thread = threading.Thread(
            target=cls.server.serve_forever,
            daemon=True,
        )
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=5)
        TEST_DATA.cleanup()

    def setUp(self):
        matcher = face_service.MATCHER
        with matcher.lock:
            matcher.enrollments.clear()
            matcher.sessions.clear()
            matcher.active_session = None
            matcher._retrain()

    def request(self, method, path, body=None, origin=None):
        import json

        data = None if body is None else json.dumps(body).encode("utf-8")
        headers = {}
        if data is not None:
            headers["Content-Type"] = "application/json"
        if origin:
            headers["Origin"] = origin
        request = Request(
            self.base_url + path,
            data=data,
            headers=headers,
            method=method,
        )
        try:
            response = urlopen(request, timeout=3)
        except HTTPError as error:
            response = error
        with response:
            payload = response.read()
            return response.status, payload

    @staticmethod
    def encoded_blank_frame():
        success, encoded = cv2.imencode(
            ".jpg", np.zeros((240, 320, 3), dtype=np.uint8)
        )
        if not success:
            raise AssertionError("Could not create a synthetic blank image.")
        return base64.b64encode(encoded.tobytes()).decode("ascii")

    def test_health_and_enrollment_listing_are_local_metadata_only(self):
        status, payload = self.request("GET", "/health")
        self.assertEqual(status, 200)
        self.assertIn(b"OpenCV Haar cascade + LBPH", payload)
        status, payload = self.request("GET", "/enrollments")
        self.assertEqual(status, 200)
        self.assertEqual(payload, b"[]")

    def test_only_loopback_vite_origins_pass_preflight(self):
        status, _ = self.request(
            "OPTIONS",
            "/recognition/sessions",
            origin="http://localhost:5175",
        )
        self.assertEqual(status, 204)
        status, _ = self.request(
            "OPTIONS",
            "/recognition/sessions",
            origin="https://example.invalid",
        )
        self.assertEqual(status, 403)

    def test_no_face_is_not_room_occupancy_or_identity(self):
        matcher = face_service.MATCHER
        matcher.start_session("room_session_123", "Bedroom")
        result = matcher.recognize(
            "room_session_123",
            "Bedroom",
            self.encoded_blank_frame(),
        )
        self.assertEqual(result["status"], "no_person")
        self.assertEqual(result["faceCount"], 0)

    def test_multiple_faces_are_ambiguous_and_room_is_bound_to_session(self):
        matcher = face_service.MATCHER
        matcher.start_session("room_session_456", "Study")
        image = cv2.imdecode(
            np.frombuffer(
                base64.b64decode(self.encoded_blank_frame()),
                dtype=np.uint8,
            ),
            cv2.IMREAD_COLOR,
        )
        with patch.object(
            matcher,
            "_faces",
            return_value=[
                np.zeros((128, 128), dtype=np.uint8),
                np.zeros((128, 128), dtype=np.uint8),
            ],
        ):
            result = matcher.recognize(
                "room_session_456", "Study", self.encoded_blank_frame()
            )
        self.assertEqual(result["status"], "ambiguous")
        self.assertEqual(result["faceCount"], 2)
        with self.assertRaises(PermissionError):
            matcher.recognize(
                "room_session_456", "Bedroom", self.encoded_blank_frame()
            )

    def test_unknown_and_consistent_match_threshold(self):
        matcher = face_service.MATCHER
        matcher.start_session("room_session_789", "Kitchen")
        matcher.enrollments["face_enroll_1234"] = {
            "displayName": "Consenting Member",
            "samples": [np.zeros((128, 128), dtype=np.uint8)],
        }
        matcher.trained = True
        matcher.label_ids = {1: "face_enroll_1234"}
        matcher.recognizer = Mock()
        matcher.recognizer.predict.return_value = (1, face_service.MATCH_THRESHOLD + 1)
        with patch.object(
            matcher,
            "_faces",
            return_value=[np.zeros((128, 128), dtype=np.uint8)],
        ):
            result = matcher.recognize(
                "room_session_789", "Kitchen", self.encoded_blank_frame()
            )
        self.assertEqual(result["status"], "unknown")

        matcher.recognizer.predict.return_value = (1, face_service.MATCH_THRESHOLD)
        with patch.object(
            matcher,
            "_faces",
            return_value=[np.zeros((128, 128), dtype=np.uint8)],
        ):
            results = [
                matcher.recognize(
                    "room_session_789", "Kitchen", self.encoded_blank_frame()
                )
                for _ in range(3)
            ]
        self.assertEqual(
            [item["status"] for item in results],
            ["pending", "pending", "matched"],
        )
        self.assertEqual(
            results[-1]["enrollmentId"],
            "face_enroll_1234",
        )

    def test_dpapi_protects_and_unprotects_bytes_on_this_windows_account(self):
        secret = b"test-only synthetic face-template marker"
        protected = face_service._protect(secret)
        self.assertNotEqual(protected, secret)
        self.assertEqual(face_service._protect(protected, decrypt=True), secret)


if __name__ == "__main__":
    unittest.main(verbosity=2)
