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

    @staticmethod
    def good_quality_face():
        rows, columns = np.indices((128, 128))
        return (((rows // 8 + columns // 8) % 2) * 180 + 30).astype(
            np.uint8
        )

    @staticmethod
    def textured_face(seed):
        rng = np.random.default_rng(seed)
        face = np.full((128, 128), 110, dtype=np.uint8)
        for y in range(8, 128, 8):
            for x in range(8, 128, 8):
                face[y : y + 8, x : x + 8] = rng.integers(35, 220)
        return face

    def test_health_and_enrollment_listing_are_local_metadata_only(self):
        status, payload = self.request("GET", "/health")
        self.assertEqual(status, 200)
        self.assertIn(b"OpenCV Haar cascade + LBPH", payload)
        self.assertIn(b'"enrollmentCount":0', payload)
        self.assertIn(b'"recognitionReady":false', payload)
        status, payload = self.request("GET", "/enrollments")
        self.assertEqual(status, 200)
        self.assertEqual(payload, b"[]")

    def test_enrollment_is_encrypted_reloaded_and_matched_by_real_lbph(self):
        import json

        store_path = Path(TEST_DATA.name) / "persistent-test" / "face.dpapi"
        enrollment_id = "persist_enroll_123"
        samples = [self.textured_face(seed) for seed in range(10, 20)]
        blank_frame = self.encoded_blank_frame()

        with patch.object(face_service, "STORE_PATH", store_path):
            matcher = face_service.FaceMatcher()
            with patch.object(face_service, "MATCHER", matcher):
                with patch.object(
                    matcher,
                    "_faces",
                    side_effect=[[sample] for sample in samples],
                ):
                    status, payload = self.request(
                        "POST",
                        f"/enrollments/{enrollment_id}/train",
                        {
                            "displayName": "Encrypted Persistence Test",
                            "frames": [blank_frame] * len(samples),
                        },
                    )
                self.assertEqual(status, 201)
                self.assertEqual(
                    json.loads(payload),
                    {
                        "enrollmentId": enrollment_id,
                        "samples": len(samples),
                        "enrollmentCount": 1,
                        "recognitionReady": True,
                    },
                )
                self.assertTrue(store_path.is_file())
                encrypted_store = store_path.read_bytes()
                self.assertNotIn(b"Encrypted Persistence Test", encrypted_store)

            restarted_matcher = face_service.FaceMatcher()
            self.assertTrue(restarted_matcher.trained)
            self.assertEqual(
                list(restarted_matcher.enrollments),
                [enrollment_id],
            )
            self.assertEqual(
                len(restarted_matcher.enrollments[enrollment_id]["samples"]),
                len(samples),
            )
            _, enrolled_distance = restarted_matcher.recognizer.predict(
                samples[3]
            )
            self.assertLessEqual(
                enrolled_distance, face_service.MATCH_THRESHOLD
            )

            with patch.object(face_service, "MATCHER", restarted_matcher):
                status, payload = self.request("GET", "/health")
                self.assertEqual(status, 200)
                health = json.loads(payload)
                self.assertEqual(health["enrollmentCount"], 1)
                self.assertTrue(health["recognitionReady"])
                status, payload = self.request("GET", "/enrollments")
                self.assertEqual(status, 200)
                self.assertEqual(json.loads(payload)[0]["id"], enrollment_id)

                restarted_matcher.start_session("persist_session_123", "Study")
                with patch.object(
                    restarted_matcher,
                    "_faces",
                    return_value=[samples[3]],
                ):
                    results = [
                        restarted_matcher.recognize(
                            "persist_session_123", "Study", blank_frame
                        )
                        for _ in range(3)
                    ]
                self.assertEqual(
                    [result["status"] for result in results],
                    ["pending", "pending", "matched"],
                )
                self.assertEqual(results[-1]["enrollmentId"], enrollment_id)

                unknown = self.textured_face(999)
                label, distance = restarted_matcher.recognizer.predict(unknown)
                self.assertIn(label, restarted_matcher.label_ids)
                self.assertGreater(distance, face_service.MATCH_THRESHOLD)
                with patch.object(
                    restarted_matcher,
                    "_faces",
                    return_value=[unknown],
                ):
                    unknown_results = [
                        restarted_matcher.recognize(
                            "persist_session_123", "Study", blank_frame
                        )
                        for _ in range(3)
                    ]
                self.assertEqual(
                    [result["status"] for result in unknown_results],
                    ["uncertain", "uncertain", "unknown"],
                )

    def test_failed_persistence_does_not_report_or_keep_an_enrollment(self):
        matcher = face_service.MATCHER
        enrollment_id = "storage_fail_123"
        faces = [self.textured_face(seed) for seed in range(20, 28)]
        with patch.object(
            matcher,
            "_faces",
            side_effect=[[face] for face in faces],
        ), patch.object(
            matcher, "_save", side_effect=PermissionError("test storage failure")
        ):
            with self.assertRaisesRegex(PermissionError, "test storage failure"):
                matcher.enroll(
                    enrollment_id,
                    "Persistence Failure Test",
                    [self.encoded_blank_frame()] * len(faces),
                )
        self.assertNotIn(enrollment_id, matcher.enrollments)
        self.assertFalse(matcher.trained)

    def test_enrollment_reports_no_face_and_multiple_faces_separately(self):
        matcher = face_service.MATCHER
        frames = [self.encoded_blank_frame()] * 8
        with patch.object(matcher, "_faces", return_value=[]):
            with self.assertRaisesRegex(ValueError, "No face was detected"):
                matcher.enroll("no_face_enroll_1", "No Face", frames)

        with patch.object(
            matcher,
            "_faces",
            return_value=[self.textured_face(71), self.textured_face(72)],
        ):
            with self.assertRaisesRegex(ValueError, "Multiple faces were detected"):
                matcher.enroll("multi_face_enroll_1", "Multiple Faces", frames)

        self.assertEqual(matcher.enrollments, {})

    def test_corrupt_encrypted_store_is_reported_not_silently_ignored(self):
        import json

        store_path = Path(TEST_DATA.name) / "corrupt-test" / "face.dpapi"
        store_path.parent.mkdir(parents=True)
        invalid_store = json.dumps({
            "face_enroll_123": {
                "displayName": "Stored member",
                "samples": [base64.b64encode(b"short").decode("ascii")] * 8,
            }
        }).encode("utf-8")
        store_path.write_bytes(face_service._protect(invalid_store))

        with patch.object(face_service, "STORE_PATH", store_path):
            with self.assertRaisesRegex(
                RuntimeError, "Encrypted face enrollments could not be read"
            ):
                face_service.FaceMatcher()

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
                self.good_quality_face(),
                self.good_quality_face(),
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
            poor_quality = matcher.recognize(
                "room_session_789", "Kitchen", self.encoded_blank_frame()
            )
        self.assertEqual(poor_quality["status"], "uncertain")
        self.assertEqual(poor_quality["reason"], "poor_quality")

        with patch.object(
            matcher,
            "_faces",
            return_value=[self.good_quality_face()],
        ):
            unknown_results = [
                matcher.recognize(
                    "room_session_789", "Kitchen", self.encoded_blank_frame()
                )
                for _ in range(3)
            ]
        self.assertEqual(
            [item["status"] for item in unknown_results],
            ["uncertain", "uncertain", "unknown"],
        )

        matcher.recognizer.predict.return_value = (1, face_service.MATCH_THRESHOLD)
        with patch.object(
            matcher,
            "_faces",
            return_value=[self.good_quality_face()],
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
        with self.assertRaises(RuntimeError):
            matcher.enroll("face_enroll_1234", "Consenting Member", [""] * 8)

    def test_poor_lighting_blur_and_consent_sample_quality_are_rejected(self):
        matcher = face_service.MATCHER
        matcher.start_session("room_session_quality", "Study")
        with patch.object(
            matcher,
            "_faces",
            return_value=[np.zeros((128, 128), dtype=np.uint8)],
        ):
            result = matcher.recognize(
                "room_session_quality", "Study", self.encoded_blank_frame()
            )
        self.assertEqual(result["status"], "uncertain")
        self.assertEqual(result["reason"], "poor_quality")
        with patch.object(
            matcher,
            "_faces",
            return_value=[np.zeros((128, 128), dtype=np.uint8)],
        ):
            with self.assertRaisesRegex(ValueError, "too dark"):
                matcher.enroll(
                    "face_enroll_quality",
                    "Consenting Member",
                    [self.encoded_blank_frame()] * 8,
                )

    def test_dpapi_protects_and_unprotects_bytes_on_this_windows_account(self):
        secret = b"test-only synthetic face-template marker"
        protected = face_service._protect(secret)
        self.assertNotEqual(protected, secret)
        self.assertEqual(face_service._protect(protected, decrypt=True), secret)


if __name__ == "__main__":
    unittest.main(verbosity=2)
