import json
import os
import sqlite3
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parent


class ProfilePreferencesApiTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if len(sys.argv) != 2:
            raise RuntimeError(
                "Usage: python test_profile_preferences_api.py <SmartHomeAPI.exe>"
            )

        cls.executable = Path(sys.argv[1]).resolve()
        if not cls.executable.is_file():
            raise RuntimeError(f"API executable not found: {cls.executable}")

        cls.temp_dir = tempfile.TemporaryDirectory(prefix="smarthome-api-test-")
        cls.database_dir = Path(cls.temp_dir.name)
        source = sqlite3.connect(
            f"file:{ROOT / 'smarthome.db'}?mode=ro",
            uri=True,
            timeout=5,
        )
        destination = sqlite3.connect(cls.database_dir / "smarthome.db")
        try:
            source.backup(destination)
            cls.baseline_device_count = destination.execute(
                "SELECT COUNT(*) FROM devices"
            ).fetchone()[0]
            bedroom_ac = destination.execute(
                "SELECT status, setting FROM devices WHERE id = ?",
                ("BEDROOM_AC",),
            ).fetchone()
            cls.bedroom_ac_baseline = (
                None if bedroom_ac is None else (bedroom_ac[0], bedroom_ac[1])
            )
        finally:
            destination.close()
            source.close()
        with socket.socket() as port_socket:
            port_socket.bind(("127.0.0.1", 0))
            cls.port = port_socket.getsockname()[1]
        cls.api_url = f"http://127.0.0.1:{cls.port}"
        cls.server = None
        cls.start_server()

    @classmethod
    def tearDownClass(cls):
        cls.stop_server()
        cls.temp_dir.cleanup()

    @classmethod
    def start_server(cls):
        environment = os.environ.copy()
        environment["SMARTHOME_API_PORT"] = str(cls.port)
        environment.pop("AI_API_KEY", None)
        environment.pop("AI_API_URL", None)
        environment.pop("AI_MODEL", None)
        cls.server = subprocess.Popen(
            [str(cls.executable)],
            cwd=cls.database_dir,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
            env=environment,
        )

        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            if cls.server.poll() is not None:
                error = cls.server.stderr.read()
                raise RuntimeError(f"API server exited during startup: {error}")

            try:
                cls.request("GET", "/api/rooms", expected=200)
                return
            except (urllib.error.URLError, TimeoutError):
                time.sleep(0.1)

        cls.stop_server()
        raise RuntimeError("API server did not become ready within 10 seconds.")

    @classmethod
    def stop_server(cls):
        if cls.server is None:
            return

        if cls.server.poll() is None:
            cls.server.terminate()
            try:
                cls.server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                cls.server.kill()
                cls.server.wait(timeout=5)

        cls.server.stderr.close()
        cls.server = None

    @classmethod
    def request(cls, method, path, body=None, expected=200):
        data = None if body is None else json.dumps(body).encode("utf-8")
        headers = {} if data is None else {"Content-Type": "application/json"}
        request = urllib.request.Request(
            f"{cls.api_url}{path}", data=data, headers=headers, method=method
        )

        try:
            response = urllib.request.urlopen(request, timeout=4)
        except urllib.error.HTTPError as error:
            response = error

        with response:
            payload = response.read().decode("utf-8")
            if response.status != expected:
                raise AssertionError(
                    f"{method} {path}: expected {expected}, "
                    f"got {response.status}: {payload}"
                )
            return response.status, json.loads(payload) if payload else {}

    def test_profiles_preferences_apply_and_restart_persistence(self):
        suffix = uuid.uuid4().hex[:10]
        profile_id = f"pref_test_{suffix}"
        second_profile_id = f"pref_other_{suffix}"
        room = f"Preference Test {suffix}"

        _, rooms = self.request("GET", "/api/rooms")
        self.assertIsInstance(rooms, list)

        self.request("POST", "/api/rooms", {"name": room}, expected=201)
        self.request(
            "POST",
            "/api/profiles",
            {"id": profile_id, "displayName": "Preference Test"},
            expected=201,
        )
        self.request(
            "POST",
            "/api/profiles",
            {"id": second_profile_id, "displayName": "Other Profile"},
            expected=201,
        )

        _, profiles = self.request("GET", "/api/profiles")
        self.assertIn(profile_id, {item["id"] for item in profiles})

        for device_type in ("AC", "Heater", "Fan", "Light"):
            self.request(
                "POST",
                "/api/devices",
                {
                    "id": f"{device_type}_{suffix}",
                    "name": f"Test {device_type}",
                    "type": device_type,
                    "roomName": room,
                    "powerWatts": 60,
                    "temporary": False,
                },
                expected=201,
            )

        encoded_profile = urllib.parse.quote(profile_id, safe="")
        encoded_room = urllib.parse.quote(room, safe="")
        preference_path = (
            f"/api/profiles/{encoded_profile}/preferences?roomName={encoded_room}"
        )
        self.request("GET", preference_path, expected=404)

        preferences = {
            "roomName": room,
            "acTemperature": 26,
            "heaterTemperature": 35,
            "fanSpeed": 4,
            "lightOn": 1,
        }
        self.request(
            "PUT",
            f"/api/profiles/{encoded_profile}/preferences",
            preferences,
        )
        _, loaded = self.request("GET", preference_path)
        self.assertEqual(loaded["acTemperature"], 26)
        self.assertEqual(loaded["heaterTemperature"], 35)
        self.assertEqual(loaded["fanSpeed"], 4)
        self.assertEqual(loaded["lightOn"], 1)

        invalid = dict(preferences, acTemperature=31)
        self.request(
            "PUT",
            f"/api/profiles/{encoded_profile}/preferences",
            invalid,
            expected=400,
        )

        second_profile_path = (
            f"/api/profiles/{urllib.parse.quote(second_profile_id, safe='')}"
            f"/preferences?roomName={encoded_room}"
        )
        self.request("GET", second_profile_path, expected=404)

        _, result = self.request(
            "POST",
            f"/api/profiles/{encoded_profile}/preferences/apply",
            {"roomName": room},
        )
        self.assertTrue(result["success"])
        self.assertEqual(len(result["actions"]), 4)
        self.assertTrue(all(item["status"] == "applied" for item in result["actions"]))

        time.sleep(1)
        _, summary = self.request("GET", "/api/energy/summary")
        self.assertTrue(summary["estimated"])
        self.assertEqual(summary["unit"], "kWh")
        self.assertEqual(
            set(summary["windows"]),
            {"last24Hours", "last7Days", "last30Days"},
        )
        daily = summary["windows"]["last24Hours"]
        self.assertGreater(daily["energyKwh"], 0)
        self.assertIn("previousEnergyKwh", daily)
        self.assertIn("changeKwh", daily)
        self.assertIn(
            f"Light_{suffix}",
            {item["deviceId"] for item in daily["byAppliance"]},
        )

        _, devices = self.request("GET", "/api/devices")
        room_devices = {item["type"]: item for item in devices if item["roomName"] == room}
        self.assertEqual(room_devices["AC"]["setting"], 26)
        self.assertEqual(room_devices["Heater"]["setting"], 35)
        self.assertEqual(room_devices["Fan"]["setting"], 4)
        self.assertEqual(room_devices["Light"]["status"], "ON")

        new_room = f"{room} Renamed"
        self.request(
            "POST",
            "/api/rooms/rename",
            {"oldName": room, "newName": new_room},
        )
        renamed_path = (
            f"/api/profiles/{encoded_profile}/preferences"
            f"?roomName={urllib.parse.quote(new_room, safe='')}"
        )
        _, renamed_preferences = self.request("GET", renamed_path)
        self.assertEqual(renamed_preferences["fanSpeed"], 4)

        missing_device_room = f"Unsupported Test {suffix}"
        self.request("POST", "/api/rooms", {"name": missing_device_room}, expected=201)
        self.request(
            "PUT",
            f"/api/profiles/{encoded_profile}/preferences",
            {
                "roomName": missing_device_room,
                "acTemperature": 24,
                "heaterTemperature": -1,
                "fanSpeed": -1,
                "lightOn": -1,
            },
        )
        _, unsupported = self.request(
            "POST",
            f"/api/profiles/{encoded_profile}/preferences/apply",
            {"roomName": missing_device_room},
        )
        self.assertFalse(unsupported["success"])
        self.assertEqual(unsupported["actions"][0]["status"], "unsupported")

        self.stop_server()
        self.start_server()
        _, after_restart = self.request("GET", renamed_path)
        self.assertEqual(after_restart["acTemperature"], 26)
        self.assertEqual(after_restart["heaterTemperature"], 35)
        self.assertEqual(after_restart["fanSpeed"], 4)
        self.assertEqual(after_restart["lightOn"], 1)

        _, devices_after_restart = self.request("GET", "/api/devices")
        restarted_room_devices = {
            item["type"]: item
            for item in devices_after_restart
            if item["roomName"] == new_room
        }
        self.assertEqual(restarted_room_devices["AC"]["setting"], 26)
        self.assertEqual(restarted_room_devices["Heater"]["setting"], 35)
        self.assertEqual(restarted_room_devices["Fan"]["setting"], 4)
        self.assertEqual(restarted_room_devices["Light"]["status"], "ON")

        self.stop_server()
        database = sqlite3.connect(self.database_dir / "smarthome.db")
        database.execute(
            "UPDATE user_preferences SET ac_temperature = 31 "
            "WHERE user_id = ? AND room_name = ?",
            (profile_id, new_room),
        )
        database.commit()
        database.close()
        self.start_server()
        _, invalid_stored = self.request("GET", renamed_path, expected=500)
        self.assertIn("outside supported appliance limits", invalid_stored["error"])

    def test_preference_route_reports_missing_profile_and_room(self):
        profile_id = urllib.parse.quote("not_a_real_profile", safe="")
        self.request(
            "GET",
            f"/api/profiles/{profile_id}/preferences?roomName=Bedroom",
            expected=404,
        )

    def test_preflight_allows_put(self):
        request = urllib.request.Request(
            self.api_url + "/api/profiles/example/preferences",
            headers={
                "Origin": "http://localhost:5173",
                "Access-Control-Request-Method": "PUT",
            },
            method="OPTIONS",
        )

        with urllib.request.urlopen(request, timeout=4) as response:
            self.assertEqual(response.status, 204)
            self.assertIn("PUT", response.headers["Access-Control-Allow-Methods"])
            self.assertEqual(
                response.headers["Access-Control-Allow-Origin"],
                "http://localhost:5173",
            )

    def test_preflight_allows_patch(self):
        request = urllib.request.Request(
            self.api_url + "/api/devices/example/settings",
            headers={
                "Origin": "http://localhost:5173",
                "Access-Control-Request-Method": "PATCH",
            },
            method="OPTIONS",
        )

        with urllib.request.urlopen(request, timeout=4) as response:
            self.assertEqual(response.status, 204)
            self.assertIn("PATCH", response.headers["Access-Control-Allow-Methods"])

    def test_energy_assistant_uses_rule_based_fallback_without_provider(self):
        _, result = self.request(
            "POST",
            "/api/assistant/chat",
            {"question": "Which appliance used the most energy?"},
        )
        self.assertEqual(result["source"], "rule_based")
        self.assertTrue(result["estimated"])
        self.assertEqual(result["unit"], "kWh")
        self.assertIn("estimate", result["answer"])
        self.assertIn("last24Hours", result["windows"])
        self.assertIn("last7Days", result["windows"])
        self.assertIn("last30Days", result["windows"])
        self.assertIn(
            "previousEnergyKwh",
            result["windows"]["last7Days"],
        )
        self.assertTrue(result["limitations"])
        self.assertTrue(any("Tariff" in item for item in result["limitations"]))
        self.assertTrue(all(
            recommendation["requiresUserAction"]
            for recommendation in result["recommendations"]
        ))

        self.request(
            "POST",
            "/api/assistant/chat",
            {"question": "   "},
            expected=400,
        )
        self.request(
            "POST",
            "/api/assistant/chat",
            {"question": "x" * 5000},
            expected=413,
        )

    def test_device_capabilities_settings_migration_and_restart(self):
        suffix = uuid.uuid4().hex[:10]
        rgb_id = f"RGB_{suffix}"
        standard_id = f"STANDARD_{suffix}"
        fan_id = f"FAN_{suffix}"
        database_path = self.database_dir / "smarthome.db"
        database = sqlite3.connect(database_path)
        original_device_count = database.execute(
            "SELECT COUNT(*) FROM devices"
        ).fetchone()[0]
        columns = {
            row[1] for row in database.execute("PRAGMA table_info(devices)")
        }
        self.assertTrue({
            "light_type", "brightness", "color", "color_temperature"
        }.issubset(columns))
        self.assertEqual(database.execute("PRAGMA user_version").fetchone()[0], 2)
        automation_tables = {
            row[0] for row in database.execute(
                "SELECT name FROM sqlite_master WHERE type = 'table'"
            )
        }
        self.assertTrue({
            "automation_enrollments",
            "automation_events",
            "automation_settings",
        }.issubset(automation_tables))
        database.close()
        self.assertEqual(original_device_count, self.baseline_device_count)

        backups = list(self.database_dir.glob("smarthome.db.pre-v2-*.bak"))
        self.assertEqual(len(backups), 1)
        backup_db = sqlite3.connect(f"file:{backups[0]}?mode=ro", uri=True)
        self.assertEqual(
            backup_db.execute("PRAGMA integrity_check").fetchone()[0],
            "ok",
        )
        backup_columns = {
            row[1] for row in backup_db.execute("PRAGMA table_info(devices)")
        }
        self.assertIn("light_type", backup_columns)
        self.assertEqual(
            backup_db.execute("PRAGMA user_version").fetchone()[0],
            1,
        )
        self.assertEqual(
            backup_db.execute("SELECT COUNT(*) FROM devices").fetchone()[0],
            self.baseline_device_count,
        )
        backup_db.close()

        _, original_devices = self.request("GET", "/api/devices")
        self.assertGreaterEqual(len(original_devices), original_device_count)
        self.assertTrue(any(item["id"] == "BEDROOM_AC" for item in original_devices))
        original_ac = next(
            item for item in original_devices if item["id"] == "BEDROOM_AC"
        )
        self.assertIsNotNone(self.bedroom_ac_baseline)
        self.assertEqual(original_ac["setting"], self.bedroom_ac_baseline[1])
        self.assertEqual(
            original_ac["status"],
            "ON" if self.bedroom_ac_baseline[0] == 1 else "OFF",
        )

        _, rgb = self.request(
            "POST",
            "/api/devices",
            {
                "id": rgb_id,
                "name": "Ambient RGB",
                "type": "Light",
                "roomName": "Living Room",
                "powerWatts": 18,
                "temporary": False,
                "initialOn": True,
                "lightType": "rgb",
                "brightness": 73,
                "color": "#12abEF",
            },
            expected=201,
        )
        self.assertEqual(rgb["status"], "ON")

        _, standard = self.request(
            "POST",
            "/api/devices",
            {
                "id": standard_id,
                "name": "Second Living Room Light",
                "type": "Light",
                "roomName": "Living Room",
                "powerWatts": 40,
                "temporary": False,
            },
            expected=201,
        )
        self.assertEqual(standard["type"], "Light")
        _, rgb = self.request("GET", f"/api/devices/{rgb_id}")
        _, standard = self.request("GET", f"/api/devices/{standard_id}")
        self.assertIn("color", rgb["capabilities"])
        self.assertNotIn("brightness", standard["capabilities"])
        self.assertEqual(rgb["color"], "#12ABEF")
        self.assertEqual(rgb["brightness"], 73)
        self.request(
            "PATCH",
            f"/api/devices/{rgb_id}",
            {"name": "Ambient RGB renamed", "powerWatts": 20},
        )
        _, renamed_rgb = self.request("GET", f"/api/devices/{rgb_id}")
        self.assertEqual(renamed_rgb["name"], "Ambient RGB renamed")
        self.assertEqual(renamed_rgb["powerWatts"], 20)
        _, sessions_after_power_edit = self.request(
            "GET", "/api/energy/sessions"
        )
        rgb_sessions = [
            item for item in sessions_after_power_edit
            if item["deviceId"] == rgb_id
        ]
        self.assertEqual(len(rgb_sessions), 2)
        self.assertEqual(
            sorted(item["powerWatts"] for item in rgb_sessions),
            [18, 20],
        )
        self.assertEqual(
            sum(item["active"] for item in rgb_sessions),
            1,
        )

        session_count_before_setting = len(rgb_sessions)
        self.request(
            "PATCH",
            f"/api/devices/{rgb_id}/settings",
            {"brightness": 48},
        )
        _, rgb = self.request("GET", f"/api/devices/{rgb_id}")
        self.assertEqual(rgb["brightness"], 48)
        _, sessions_after_setting = self.request(
            "GET", "/api/energy/sessions"
        )
        self.assertEqual(
            len([
                item for item in sessions_after_setting
                if item["deviceId"] == rgb_id
            ]),
            session_count_before_setting,
        )
        self.request(
            "PATCH",
            f"/api/devices/{standard_id}/settings",
            {"brightness": 40},
            expected=400,
        )
        self.request(
            "PATCH",
            f"/api/devices/{rgb_id}/settings",
            {"color": "invalid"},
            expected=400,
        )

        self.request(
            "POST",
            "/api/devices",
            {
                "id": fan_id,
                "name": "Second Living Room Fan",
                "type": "Fan",
                "roomName": "Living Room",
                "powerWatts": 70,
                "temporary": False,
                "setting": 3,
            },
            expected=201,
        )
        self.request(
            "PATCH",
            f"/api/devices/{fan_id}/settings",
            {"setting": 5},
        )
        _, fan = self.request("GET", f"/api/devices/{fan_id}")
        self.assertEqual(fan["setting"], 5)
        self.request(
            "PATCH",
            f"/api/devices/{fan_id}/settings",
            {"setting": 6},
            expected=400,
        )
        self.request(
            "POST",
            "/api/devices",
            {
                "id": f"BAD_ROOM_{suffix}",
                "name": "Invalid room device",
                "type": "Fan",
                "roomName": f"No such room {suffix}",
                "powerWatts": 75,
                "temporary": False,
            },
            expected=404,
        )

        ac_id = f"AC_{suffix}"
        heater_id = f"HEATER_{suffix}"
        tunable_id = f"TUNABLE_{suffix}"
        for device_id, device_type, setting in (
            (ac_id, "AC", 27),
            (heater_id, "Heater", 32),
        ):
            self.request(
                "POST",
                "/api/devices",
                {
                    "id": device_id,
                    "name": f"Test {device_type}",
                    "type": device_type,
                    "roomName": "Living Room",
                    "powerWatts": 900,
                    "temporary": False,
                    "setting": setting,
                },
                expected=201,
            )
        self.request(
            "PATCH",
            f"/api/devices/{ac_id}/settings",
            {"setting": 30},
        )
        self.request(
            "PATCH",
            f"/api/devices/{ac_id}/settings",
            {"setting": 31},
            expected=400,
        )
        self.request(
            "PATCH",
            f"/api/devices/{heater_id}/settings",
            {"setting": 44},
        )

        self.request(
            "POST",
            "/api/devices",
            {
                "id": tunable_id,
                "name": "Tunable White",
                "type": "Light",
                "roomName": "Living Room",
                "powerWatts": 12,
                "temporary": False,
                "lightType": "tunable-white",
                "brightness": 62,
                "colorTemperature": 3500,
            },
            expected=201,
        )
        self.request(
            "PATCH",
            f"/api/devices/{tunable_id}/settings",
            {"colorTemperature": 5000},
        )
        _, tunable = self.request("GET", f"/api/devices/{tunable_id}")
        self.assertEqual(tunable["colorTemperature"], 5000)
        self.assertIn("colorTemperature", tunable["capabilities"])
        self.request(
            "PATCH",
            f"/api/devices/{tunable_id}/settings",
            {"color": "#ABCDEF"},
            expected=400,
        )
        self.request(
            "PATCH",
            f"/api/devices/not_a_device/settings",
            {"setting": 2},
            expected=404,
        )
        self.request(
            "PATCH",
            f"/api/devices/{rgb_id}/settings",
            {"brightness": "20"},
            expected=400,
        )
        _, existing_rgb = self.request("GET", f"/api/devices/{rgb_id}")
        self.request(
            "POST",
            f"/api/devices/{rgb_id}/power",
            {"on": False},
        )
        self.request(
            "POST",
            f"/api/devices/{rgb_id}/power",
            {"on": True},
        )
        _, powered_rgb = self.request("GET", f"/api/devices/{rgb_id}")
        self.assertEqual(powered_rgb["color"], existing_rgb["color"])
        self.assertEqual(powered_rgb["brightness"], existing_rgb["brightness"])

        self.stop_server()
        self.start_server()
        _, restored_rgb = self.request("GET", f"/api/devices/{rgb_id}")
        _, restored_fan = self.request("GET", f"/api/devices/{fan_id}")
        _, restored_ac = self.request("GET", f"/api/devices/{ac_id}")
        _, restored_heater = self.request("GET", f"/api/devices/{heater_id}")
        _, restored_tunable = self.request("GET", f"/api/devices/{tunable_id}")
        self.assertEqual(restored_rgb["brightness"], 48)
        self.assertEqual(restored_rgb["color"], "#12ABEF")
        self.assertEqual(restored_rgb["status"], "ON")
        self.assertEqual(restored_rgb["name"], "Ambient RGB renamed")
        self.assertEqual(restored_rgb["powerWatts"], 20)
        self.assertEqual(restored_fan["setting"], 5)
        self.assertEqual(restored_ac["setting"], 30)
        self.assertEqual(restored_heater["setting"], 44)
        self.assertEqual(restored_tunable["colorTemperature"], 5000)

        database = sqlite3.connect(database_path)
        self.assertEqual(
            database.execute("SELECT COUNT(*) FROM devices").fetchone()[0],
            original_device_count + 6,
        )
        database.close()

    def test_room_scoped_camera_automation_and_confirmed_apply(self):
        suffix = uuid.uuid4().hex[:10]
        room = f"Automation Test {suffix}"
        other_room = f"Other Automation Test {suffix}"
        profile_id = f"automation_profile_{suffix}"
        empty_profile_id = f"automation_empty_{suffix}"

        _, initial_status = self.request(
            "GET", "/api/automation/status"
        )
        self.assertFalse(initial_status["enabled"])
        self.assertIsNone(initial_status["activeSession"])
        self.request("POST", "/api/rooms", {"name": room}, expected=201)
        self.request(
            "POST",
            "/api/rooms",
            {"name": other_room},
            expected=201,
        )
        self.request(
            "POST",
            "/api/automation/sessions",
            {"roomName": room},
            expected=403,
        )
        self.request(
            "PUT",
            "/api/automation/settings",
            {"enabled": True},
        )
        self.request(
            "POST",
            "/api/automation/sessions",
            {"roomName": f"missing_{suffix}"},
            expected=404,
        )
        self.request(
            "POST",
            "/api/profiles",
            {"id": profile_id, "displayName": "Automation Member"},
            expected=201,
        )
        self.request(
            "POST",
            "/api/profiles",
            {"id": empty_profile_id, "displayName": "No Preferences"},
            expected=201,
        )
        self.request(
            "POST",
            "/api/devices",
            {
                "id": f"AUTO_AC_{suffix}",
                "name": "Automation AC",
                "type": "AC",
                "roomName": room,
                "powerWatts": 900,
                "temporary": False,
                "setting": 22,
            },
            expected=201,
        )
        self.request(
            "POST",
            "/api/devices",
            {
                "id": f"AUTO_FAN_{suffix}",
                "name": "Automation Fan",
                "type": "Fan",
                "roomName": room,
                "powerWatts": 60,
                "temporary": False,
                "setting": 2,
            },
            expected=201,
        )
        self.request(
            "POST",
            "/api/devices",
            {
                "id": f"AUTO_LIGHT_{suffix}",
                "name": "Automation Light",
                "type": "Light",
                "roomName": room,
                "powerWatts": 45,
                "temporary": False,
                "initialOn": False,
            },
            expected=201,
        )
        self.request(
            "PUT",
            f"/api/profiles/{profile_id}/preferences",
            {
                "roomName": room,
                "acTemperature": 25,
                "heaterTemperature": -1,
                "fanSpeed": 4,
                "lightOn": 1,
            },
        )
        _, enrollments_before = self.request(
            "GET", "/api/automation/enrollments"
        )
        self.assertEqual(enrollments_before, [])
        self.request(
            "POST",
            "/api/automation/enrollments",
            {"profileId": profile_id, "consent": False},
            expected=400,
        )
        self.request(
            "POST",
            "/api/automation/enrollments",
            {"profileId": f"missing_{suffix}", "consent": True},
            expected=404,
        )
        _, enrollment = self.request(
            "POST",
            "/api/automation/enrollments",
            {"profileId": profile_id, "consent": True},
            expected=201,
        )
        _, empty_enrollment = self.request(
            "POST",
            "/api/automation/enrollments",
            {"profileId": empty_profile_id, "consent": True},
            expected=201,
        )
        self.assertNotIn("template", enrollment)
        self.assertEqual(enrollment["profileId"], profile_id)

        _, session = self.request(
            "POST",
            "/api/automation/sessions",
            {"roomName": room},
            expected=201,
        )
        session_id = session["sessionId"]
        self.assertEqual(session["roomName"], room)
        self.request(
            "POST",
            "/api/automation/sessions",
            {"roomName": other_room},
            expected=409,
        )
        _, active = self.request(
            "GET",
            f"/api/automation/sessions/{session_id}",
        )
        self.assertEqual(active["roomName"], room)
        self.request(
            "POST",
            f"/api/automation/sessions/{session_id}/observation",
            {"roomName": other_room, "status": "matched",
             "enrollmentId": enrollment["id"]},
            expected=409,
        )
        self.request(
            "POST",
            f"/api/automation/sessions/{session_id}/observation",
            {"roomName": room, "status": "ambiguous"},
        )
        self.request(
            "POST",
            f"/api/automation/sessions/{session_id}/resolve",
            {"roomName": room, "enrollmentId": enrollment["id"]},
            expected=409,
        )
        self.request(
            "POST",
            f"/api/automation/sessions/{session_id}/observation",
            {"roomName": room, "status": "unknown"},
        )
        self.request(
            "POST",
            f"/api/automation/sessions/{session_id}/resolve",
            {"roomName": room, "enrollmentId": enrollment["id"]},
            expected=409,
        )
        self.request(
            "POST",
            f"/api/automation/sessions/{session_id}/observation",
            {"roomName": room, "status": "matched",
             "enrollmentId": empty_enrollment["id"]},
        )
        self.request(
            "POST",
            f"/api/automation/sessions/{session_id}/resolve",
            {"roomName": room, "enrollmentId": empty_enrollment["id"]},
            expected=404,
        )
        self.request(
            "POST",
            f"/api/automation/sessions/{session_id}/observation",
            {"roomName": room, "status": "matched",
             "enrollmentId": enrollment["id"]},
        )
        _, preview = self.request(
            "POST",
            f"/api/automation/sessions/{session_id}/resolve",
            {"roomName": room, "enrollmentId": enrollment["id"]},
        )
        self.assertEqual(preview["profileId"], profile_id)
        self.assertEqual(preview["roomName"], room)
        self.assertEqual(
            {item.get("deviceId") for item in preview["preview"]},
            {
                f"AUTO_AC_{suffix}",
                f"AUTO_FAN_{suffix}",
                f"AUTO_LIGHT_{suffix}",
            },
        )
        _, repeated_preview = self.request(
            "POST",
            f"/api/automation/sessions/{session_id}/resolve",
            {"roomName": room, "enrollmentId": enrollment["id"]},
        )
        self.assertEqual(repeated_preview["preview"], preview["preview"])

        apply_url = f"/api/profiles/{profile_id}/preferences/apply"
        self.request(
            "POST",
            apply_url,
            {"roomName": room, "automationSessionId": session_id},
            expected=400,
        )
        _, applied = self.request(
            "POST",
            apply_url,
            {
                "roomName": room,
                "automationSessionId": session_id,
                "confirmed": True,
            },
        )
        self.assertTrue(applied["success"])
        self.assertTrue(applied["eventPersisted"])
        self.assertEqual(
            {item["status"] for item in applied["actions"]},
            {"applied"},
        )
        self.request(
            "POST",
            apply_url,
            {
                "roomName": room,
                "automationSessionId": session_id,
                "confirmed": True,
            },
            expected=409,
        )
        self.request(
            "POST",
            f"/api/automation/sessions/{session_id}/observation",
            {"roomName": room, "status": "no_person"},
        )
        self.request(
            "POST",
            apply_url,
            {
                "roomName": room,
                "automationSessionId": session_id,
                "confirmed": True,
            },
            expected=409,
        )
        _, events = self.request("GET", "/api/automation/events?limit=10")
        self.assertIn(
            "applied",
            {event["outcome"] for event in events},
        )
        self.assertIn(
            "missing_preferences",
            {event["outcome"] for event in events},
        )
        self.assertTrue(all("settingsChanged" in event for event in events))

        self.request(
            "DELETE",
            f"/api/automation/sessions/{session_id}",
        )
        self.request(
            "PUT",
            "/api/automation/settings",
            {"enabled": False},
        )
        _, persisted_status = self.request(
            "GET", "/api/automation/status"
        )
        self.assertFalse(persisted_status["enabled"])
        self.assertIsNone(persisted_status["activeSession"])
        self.request(
            "POST",
            "/api/automation/sessions",
            {"roomName": room},
            expected=403,
        )
        self.request(
            "DELETE",
            f"/api/automation/enrollments/{enrollment['id']}",
        )
        self.request(
            "DELETE",
            f"/api/automation/enrollments/{empty_enrollment['id']}",
        )


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
