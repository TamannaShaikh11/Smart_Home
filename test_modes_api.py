import json
import os
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request
import uuid
from pathlib import Path


ROOT = Path(__file__).resolve().parent


class ModesApiTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if len(sys.argv) != 2:
            raise RuntimeError(
                "Usage: python test_modes_api.py <SmartHomeAPI.exe>"
            )
        cls.executable = Path(sys.argv[1]).resolve()
        if not cls.executable.is_file():
            raise RuntimeError(f"API executable not found: {cls.executable}")
        cls.temp_dir = tempfile.TemporaryDirectory(prefix="smarthome-modes-")
        cls.database_dir = Path(cls.temp_dir.name)
        source = sqlite3.connect(
            f"file:{ROOT / 'smarthome.db'}?mode=ro", uri=True, timeout=5
        )
        destination = sqlite3.connect(cls.database_dir / "smarthome.db")
        try:
            source.backup(destination)
            cls.baseline_counts = {
                table: destination.execute(
                    f"SELECT COUNT(*) FROM {table}"
                ).fetchone()[0]
                for table in ("devices", "rooms", "user_profiles", "user_preferences")
            }
            cls.baseline_schema_version = destination.execute(
                "PRAGMA user_version"
            ).fetchone()[0]
            tables = {
                row[0] for row in destination.execute(
                    "SELECT name FROM sqlite_master WHERE type = 'table'"
                )
            }
            cls.baseline_mode_presets = {}
            cls.baseline_mode_history_count = 0
            if "mode_presets" in tables:
                cls.baseline_mode_presets = {
                    (mode_type, preset_id): json.loads(overrides)
                    for mode_type, preset_id, overrides in destination.execute(
                        "SELECT mode_type, preset_id, overrides_json "
                        "FROM mode_presets"
                    )
                }
            if "mode_history" in tables:
                cls.baseline_mode_history_count = destination.execute(
                    "SELECT COUNT(*) FROM mode_history"
                ).fetchone()[0]
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
                raise RuntimeError(
                    f"API server exited during startup: {cls.server.stderr.read()}"
                )
            try:
                cls.request("GET", "/api/rooms")
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
    def restart_server(cls):
        cls.stop_server()
        cls.start_server()

    @classmethod
    def request(cls, method, path, body=None, expected=200):
        data = None if body is None else json.dumps(body).encode("utf-8")
        headers = {} if data is None else {"Content-Type": "application/json"}
        request = urllib.request.Request(
            f"{cls.api_url}{path}", data=data, headers=headers, method=method
        )
        try:
            response = urllib.request.urlopen(request, timeout=5)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            payload = response.read().decode("utf-8")
            if response.status != expected:
                raise AssertionError(
                    f"{method} {path}: expected {expected}, got "
                    f"{response.status}: {payload}"
                )
            return json.loads(payload) if payload else {}

    def test_modes_preview_apply_restore_customization_and_history(self):
        suffix = uuid.uuid4().hex[:9]
        rooms = {
            "bedroom": f"Mode Bedroom {suffix}",
            "study": f"Mode Study {suffix}",
            "living": f"Mode Living {suffix}",
            "empty": f"Mode Empty {suffix}",
        }
        for room in rooms.values():
            self.request("POST", "/api/rooms", {"name": room}, 201)

        ids = {
            "fan": f"MODE_FAN_{suffix}",
            "ac": f"MODE_AC_{suffix}",
            "heater": f"MODE_HEATER_{suffix}",
            "rgb": f"MODE_RGB_{suffix}",
            "standard": f"MODE_STANDARD_{suffix}",
            "study_light": f"MODE_STUDY_LIGHT_{suffix}",
            "study_heater": f"MODE_STUDY_HEATER_{suffix}",
            "living_fan": f"MODE_LIVING_FAN_{suffix}",
        }
        devices = [
            (ids["fan"], "Fan", rooms["bedroom"], {}),
            (ids["ac"], "AC", rooms["bedroom"], {}),
            (ids["heater"], "Heater", rooms["bedroom"], {}),
            (ids["rgb"], "Light", rooms["bedroom"], {
                "lightType": "rgb", "brightness": 73, "color": "#112233",
                "initialOn": True,
            }),
            (ids["standard"], "Light", rooms["bedroom"], {}),
            (ids["study_light"], "Light", rooms["study"], {
                "lightType": "tunable-white", "brightness": 60,
                "colorTemperature": 4000
            }),
            (ids["study_heater"], "Heater", rooms["study"], {}),
            (ids["living_fan"], "Fan", rooms["living"], {}),
        ]
        for device_id, device_type, room, extra in devices:
            self.request(
                "POST",
                "/api/devices",
                {
                    "id": device_id,
                    "name": device_id,
                    "type": device_type,
                    "roomName": room,
                    "powerWatts": 60,
                    "temporary": False,
                    **extra,
                },
                201,
            )
        self.request(
            "PATCH",
            f"/api/devices/{ids['fan']}/settings",
            {"setting": 5},
        )
        self.request(
            "PATCH",
            f"/api/devices/{ids['ac']}/settings",
            {"setting": 29},
        )

        seasons = self.request("GET", "/api/modes/seasons")
        moods = self.request("GET", "/api/modes/moods")
        self.assertEqual(
            {item["id"] for item in seasons},
            {"summer", "monsoon", "winter", "spring"},
        )
        self.assertEqual(
            {item["id"] for item in moods},
            {"focus", "relax", "romantic", "movie-night", "sleep", "energize"},
        )
        baseline_presets = {
            ("season", item["id"]): item for item in seasons
        }
        baseline_presets.update(
            {("mood", item["id"]): item for item in moods}
        )
        for key, item in baseline_presets.items():
            self.assertEqual(
                item["overrides"],
                self.baseline_mode_presets.get(key, []),
            )

        summer_preview = self.request(
            "POST",
            "/api/modes/preview",
            {
                "seasonId": "summer",
                "moodId": "",
                "roomNames": [rooms["bedroom"], rooms["living"]],
                "deviceIds": [ids["fan"], ids["living_fan"]],
            },
        )
        season_fan_values = {
            item["deviceId"]: item["value"]
            for item in summer_preview["actions"]
            if item["property"] == "setting"
        }
        self.assertEqual(season_fan_values[ids["fan"]], 2)
        self.assertEqual(season_fan_values[ids["living_fan"]], 3)

        winter_preview = self.request(
            "POST",
            "/api/modes/preview",
            {
                "seasonId": "winter",
                "moodId": "",
                "roomNames": [rooms["bedroom"], rooms["study"]],
                "deviceIds": [ids["heater"], ids["study_heater"]],
            },
        )
        winter_heater_values = {
            item["deviceId"]: item["value"]
            for item in winter_preview["actions"]
            if item["property"] == "setting"
        }
        self.assertEqual(winter_heater_values[ids["heater"]], 21)
        self.assertEqual(winter_heater_values[ids["study_heater"]], 20)

        for season in ("summer", "monsoon", "winter", "spring"):
            preview = self.request(
                "POST",
                "/api/modes/preview",
                {
                    "seasonId": season,
                    "moodId": "",
                    "roomNames": list(rooms.values()),
                    "deviceIds": [],
                },
            )
            self.assertTrue(preview["previewId"])
            self.assertTrue(preview["actions"])
        for mood in ("focus", "relax", "romantic", "movie-night", "sleep", "energize"):
            preview = self.request(
                "POST",
                "/api/modes/preview",
                {
                    "seasonId": "",
                    "moodId": mood,
                    "roomNames": list(rooms.values()),
                    "deviceIds": [],
                },
            )
            self.assertTrue(preview["previewId"])

        empty_preview = self.request(
            "POST",
            "/api/modes/preview",
            {
                "seasonId": "summer",
                "moodId": "",
                "roomNames": [rooms["empty"]],
                "deviceIds": [],
            },
        )
        self.assertEqual(empty_preview["actions"], [])
        self.assertIn("No compatible", empty_preview["summary"]["message"])
        self.request(
            "POST",
            "/api/modes/preview",
            {"seasonId": "unknown", "moodId": "", "roomNames": [], "deviceIds": []},
            400,
        )
        self.request(
            "POST",
            "/api/modes/preview",
            {"seasonId": "summer", "moodId": "", "roomNames": ["missing"], "deviceIds": []},
            400,
        )
        self.request(
            "POST",
            "/api/modes/preview",
            {"seasonId": "summer", "moodId": "", "roomNames": [], "deviceIds": ["missing"]},
            400,
        )
        self.request(
            "POST",
            "/api/modes/preview",
            {"seasonId": "", "moodId": "", "roomNames": [], "deviceIds": []},
            400,
        )
        self.request(
            "POST",
            "/api/modes/restore",
            {"confirmed": True, "roomName": rooms["empty"]},
            404,
        )

        standard = self.request("GET", f"/api/devices/{ids['standard']}")
        self.request(
            "PUT",
            "/api/modes/presets/mood/romantic",
            {"overrides": [{
                "deviceId": ids["standard"],
                "property": "color",
                "value": "#D96B78",
            }]},
            400,
        )
        self.request(
            "PUT",
            "/api/modes/presets/mood/relax",
            {"overrides": [{
                "deviceId": ids["rgb"],
                "property": "brightness",
                "value": 101,
            }]},
            400,
        )
        self.request(
            "PUT",
            "/api/modes/presets/season/summer",
            {"overrides": [{
                "deviceId": "missing",
                "property": "setting",
                "value": 4,
            }]},
            404,
        )
        self.request(
            "PUT",
            "/api/modes/presets/season/summer",
            {"overrides": [{
                "deviceId": ids["ac"],
                "property": "mode",
                "value": "cool",
            }]},
            400,
        )

        preset_override = {
            "deviceId": ids["fan"],
            "property": "setting",
            "value": 4,
        }
        self.request(
            "PUT",
            "/api/modes/presets/season/summer",
            {"overrides": [preset_override]},
        )
        self.restart_server()
        seasons = self.request("GET", "/api/modes/seasons")
        summer = next(item for item in seasons if item["id"] == "summer")
        self.assertEqual(
            summer["overrides"],
            [{**preset_override, "roomName": rooms["bedroom"]}],
        )
        movie_power_override = {
            "deviceId": ids["rgb"],
            "property": "power",
            "value": False,
        }
        self.request(
            "PUT",
            "/api/modes/presets/mood/movie-night",
            {"overrides": [movie_power_override]},
        )
        scoped_movie_preview = self.request(
            "POST",
            "/api/modes/preview",
            {
                "seasonId": "",
                "moodId": "movie-night",
                "roomNames": [rooms["study"]],
                "deviceIds": [],
            },
        )
        self.assertFalse(any(
            item["deviceId"] == ids["rgb"]
            for item in scoped_movie_preview["actions"]
        ))

        combo = {
            "seasonId": "summer",
            "moodId": "relax",
            "roomNames": [rooms["bedroom"]],
            "deviceIds": [ids["fan"], ids["ac"]],
        }
        preview = self.request("POST", "/api/modes/preview", combo)
        fan_setting = next(
            item for item in preview["actions"]
            if item["deviceId"] == ids["fan"] and item["property"] == "setting"
        )
        self.assertEqual(fan_setting["value"], 1)
        self.assertEqual(fan_setting["source"], "mood")
        self.assertIn("overrides", fan_setting["reason"])
        self.assertEqual(
            next(
                item for item in preview["actions"]
                if item["deviceId"] == ids["ac"] and
                item["property"] == "setting"
            )["value"],
            24,
        )

        canceled_value = self.request(
            "GET", f"/api/devices/{ids['fan']}"
        )["setting"]
        self.assertEqual(canceled_value, 5)
        self.request(
            "POST",
            "/api/modes/apply",
            {"previewId": preview["previewId"], "confirmed": False},
            400,
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{ids['fan']}")["setting"],
            canceled_value,
        )

        movie_preview = self.request(
            "POST",
            "/api/modes/preview",
            {
                "seasonId": "",
                "moodId": "movie-night",
                "roomNames": [rooms["bedroom"]],
                "deviceIds": [ids["rgb"], ids["standard"]],
            },
        )
        self.assertEqual(movie_preview["summary"]["proposed"], 1)
        self.assertEqual(movie_preview["summary"]["skipped"], 0)
        standard_action = next(
            item for item in movie_preview["actions"]
            if item["deviceId"] == ids["standard"]
        )
        self.assertEqual(standard_action["property"], "policy")
        self.assertEqual(standard_action["value"], "KEEP_CURRENT")
        movie_apply = self.request(
            "POST",
            "/api/modes/apply",
            {"previewId": movie_preview["previewId"], "confirmed": True},
        )
        self.assertEqual(movie_apply["outcome"], "applied")
        self.assertEqual(movie_apply["summary"]["applied"], 1)
        self.assertEqual(movie_apply["summary"]["skipped"], 0)
        self.assertTrue(movie_apply["success"])
        self.assertEqual(
            self.request("GET", f"/api/devices/{ids['rgb']}")["brightness"],
            73,
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{ids['rgb']}")["status"],
            "OFF",
        )
        self.request(
            "POST",
            "/api/modes/apply",
            {"previewId": movie_preview["previewId"], "confirmed": True},
            409,
        )
        restored = self.request(
            "POST", "/api/modes/restore", {"confirmed": True}
        )
        self.assertTrue(restored["success"])
        self.assertEqual(
            self.request("GET", f"/api/devices/{ids['rgb']}")["brightness"],
            73,
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{ids['rgb']}")["status"],
            "ON",
        )

        preview = self.request("POST", "/api/modes/preview", combo)
        applied = self.request(
            "POST",
            "/api/modes/apply",
            {"previewId": preview["previewId"], "confirmed": True},
        )
        self.assertEqual(applied["outcome"], "applied")
        self.assertTrue(applied["historyPersisted"])
        self.assertEqual(
            self.request("GET", f"/api/devices/{ids['fan']}")["setting"],
            1,
        )
        history_before_duplicate = self.request(
            "GET", "/api/modes/history?limit=50"
        )
        unchanged = self.request("POST", "/api/modes/preview", combo)
        self.assertEqual(unchanged["summary"]["proposed"], 0)
        unchanged_result = self.request(
            "POST",
            "/api/modes/apply",
            {"previewId": unchanged["previewId"], "confirmed": True},
        )
        self.assertEqual(unchanged_result["outcome"], "no_changes")
        self.assertTrue(unchanged_result["historyEntryCreated"])
        history_after_first_no_change = self.request(
            "GET", "/api/modes/history?limit=50"
        )
        self.assertEqual(
            len(history_after_first_no_change),
            len(history_before_duplicate) + 1,
        )
        repeated_unchanged = self.request("POST", "/api/modes/preview", combo)
        repeated_result = self.request(
            "POST",
            "/api/modes/apply",
            {"previewId": repeated_unchanged["previewId"], "confirmed": True},
        )
        self.assertFalse(repeated_result["historyEntryCreated"])
        self.assertEqual(
            len(self.request("GET", "/api/modes/history?limit=50")),
            len(history_after_first_no_change),
        )

        empty_scope = {
            "seasonId": "summer",
            "moodId": "",
            "roomNames": [rooms["empty"]],
            "deviceIds": [],
        }
        empty_scope_preview = self.request(
            "POST", "/api/modes/preview", empty_scope
        )
        self.assertEqual(empty_scope_preview["actions"], [])
        empty_scope_result = self.request(
            "POST",
            "/api/modes/apply",
            {
                "previewId": empty_scope_preview["previewId"],
                "confirmed": True,
            },
        )
        self.assertEqual(empty_scope_result["outcome"], "no_changes")
        self.assertTrue(empty_scope_result["historyEntryCreated"])
        empty_scope_history = self.request(
            "GET", "/api/modes/history?limit=50"
        )
        empty_entry = empty_scope_history[0]
        self.assertEqual(
            empty_entry["selection"]["roomNames"], [rooms["empty"]]
        )
        self.assertEqual(empty_entry["actions"], [])
        repeated_empty_scope = self.request(
            "POST", "/api/modes/preview", empty_scope
        )
        repeated_empty_result = self.request(
            "POST",
            "/api/modes/apply",
            {
                "previewId": repeated_empty_scope["previewId"],
                "confirmed": True,
            },
        )
        self.assertFalse(repeated_empty_result["historyEntryCreated"])

        self.restart_server()
        history_after_restart = self.request(
            "GET", "/api/modes/history?limit=50"
        )
        self.assertGreaterEqual(len(history_after_restart), 2)
        self.assertTrue(any(item["canRestore"] for item in history_after_restart))
        restored = self.request(
            "POST", "/api/modes/restore", {"confirmed": True}
        )
        self.assertTrue(restored["success"])
        self.assertEqual(
            self.request("GET", f"/api/devices/{ids['fan']}")["setting"],
            5,
        )

        injected = sqlite3.connect(self.database_dir / "smarthome.db")
        injected.execute(
            f"""
            CREATE TRIGGER fail_one_mode_brightness
            BEFORE UPDATE OF brightness ON devices
            WHEN OLD.id = '{ids["study_light"]}' AND NEW.brightness != OLD.brightness
            BEGIN
                SELECT RAISE(FAIL, 'injected mode setting failure');
            END;
            """
        )
        injected.commit()
        injected.close()
        partial_preview = self.request(
            "POST",
            "/api/modes/preview",
            {
                "seasonId": "",
                "moodId": "energize",
                "roomNames": [rooms["bedroom"], rooms["study"]],
                "deviceIds": [ids["rgb"], ids["study_light"]],
            },
        )
        partial_apply = self.request(
            "POST",
            "/api/modes/apply",
            {
                "previewId": partial_preview["previewId"],
                "confirmed": True,
            },
        )
        self.assertEqual(partial_apply["outcome"], "partial")
        self.assertEqual(partial_apply["summary"]["failed"], 1)
        self.assertEqual(partial_apply["summary"]["applied"], 2)
        failed_brightness = next(
            action for action in partial_apply["actions"]
            if action["deviceId"] == ids["study_light"] and
            action["property"] == "brightness"
        )
        self.assertEqual(failed_brightness["status"], "failed")
        injected = sqlite3.connect(self.database_dir / "smarthome.db")
        injected.execute("DROP TRIGGER fail_one_mode_brightness")
        injected.commit()
        injected.close()
        restored_partial = self.request(
            "POST", "/api/modes/restore", {"confirmed": True}
        )
        self.assertTrue(restored_partial["success"])
        self.assertEqual(
            self.request("GET", f"/api/devices/{ids['study_light']}")["brightness"],
            60,
        )

        database = sqlite3.connect(
            f"file:{self.database_dir / 'smarthome.db'}?mode=ro", uri=True
        )
        self.assertEqual(database.execute("PRAGMA user_version").fetchone()[0], 4)
        for table, original_count in self.baseline_counts.items():
            self.assertEqual(
                database.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0],
                original_count + (4 if table == "rooms" else len(devices) if table == "devices" else 0),
            )
        self.assertGreater(
            database.execute("SELECT COUNT(*) FROM mode_history").fetchone()[0],
            self.baseline_mode_history_count,
        )
        self.assertGreater(
            database.execute("SELECT COUNT(*) FROM mode_presets").fetchone()[0],
            len(self.baseline_mode_presets),
        )
        changed_presets = {
            ("season", "summer"),
            ("mood", "relax"),
            ("mood", "movie-night"),
        }
        persisted_presets = {
            (mode_type, preset_id): json.loads(overrides)
            for mode_type, preset_id, overrides in database.execute(
                "SELECT mode_type, preset_id, overrides_json FROM mode_presets"
            )
        }
        for key, overrides in self.baseline_mode_presets.items():
            if key not in changed_presets:
                self.assertEqual(persisted_presets[key], overrides)
        database.close()
        backups = list(self.database_dir.glob("smarthome.db.pre-v3-*.bak"))
        self.assertEqual(
            len(backups),
            1 if self.baseline_schema_version < 3 else 0,
        )
        if backups:
            backup = sqlite3.connect(f"file:{backups[0]}?mode=ro", uri=True)
            self.assertEqual(backup.execute("PRAGMA user_version").fetchone()[0], 2)
            backup.close()
        self.assertIsNotNone(standard)

        policy_room = f"Mode Policy Room {suffix}"
        self.request("POST", "/api/rooms", {"name": policy_room}, 201)
        policy_ids = {
            "ac": f"POLICY_AC_{suffix}",
            "ac_second": f"POLICY_AC2_{suffix}",
            "fan": f"POLICY_FAN_{suffix}",
            "heater": f"POLICY_HEATER_{suffix}",
            "rgb": f"POLICY_RGB_{suffix}",
            "standard": f"POLICY_STANDARD_{suffix}",
        }
        policy_devices = [
            (policy_ids["ac"], "AC", False, {}),
            (policy_ids["ac_second"], "AC", True, {}),
            (policy_ids["fan"], "Fan", False, {}),
            (policy_ids["heater"], "Heater", False, {}),
            (policy_ids["rgb"], "Light", False, {
                "lightType": "rgb", "initialOn": False,
            }),
            (policy_ids["standard"], "Light", True, {"initialOn": True}),
        ]
        for device_id, device_type, _, extra in policy_devices:
            self.request(
                "POST",
                "/api/devices",
                {
                    "id": device_id,
                    "name": device_id,
                    "type": device_type,
                    "roomName": policy_room,
                    "powerWatts": 60,
                    "temporary": False,
                    **extra,
                },
                201,
            )
        for device_id, setting in (
            (policy_ids["ac"], 29),
            (policy_ids["ac_second"], 27),
            (policy_ids["fan"], 5),
            (policy_ids["heater"], 20),
        ):
            self.request(
                "PATCH",
                f"/api/devices/{device_id}/settings",
                {"setting": setting},
            )

        self.request(
            "PUT",
            "/api/modes/presets/season/summer",
            {"overrides": [{
                "deviceId": policy_ids["ac_second"],
                "property": "policy",
                "value": "OFF",
            }]},
        )
        self.request(
            "PUT",
            "/api/modes/presets/season/winter",
            {"overrides": [{
                "deviceId": policy_ids["fan"],
                "property": "policy",
                "value": "CONFIGURE_ONLY",
            }]},
        )
        self.request(
            "PUT",
            "/api/modes/presets/season/spring",
            {"overrides": [{
                "deviceId": policy_ids["fan"],
                "property": "policy",
                "value": "NOT_A_POLICY",
            }]},
            400,
        )
        self.restart_server()
        summer_preset = next(
            preset for preset in self.request("GET", "/api/modes/seasons")
            if preset["id"] == "summer"
        )
        self.assertEqual(
            next(
                rule for rule in summer_preset["overrides"]
                if rule["deviceId"] == policy_ids["ac_second"]
            )["value"],
            "OFF",
        )

        summer_room_preview = self.request(
            "POST",
            "/api/modes/preview",
            {
                "seasonId": "summer",
                "moodId": "",
                "roomNames": [policy_room],
                "deviceIds": [],
            },
        )
        self.assertEqual(
            {action["deviceId"] for action in summer_room_preview["actions"]},
            set(policy_ids.values()),
        )
        summer_policies = {
            action["deviceId"]: action["policy"]
            for action in summer_room_preview["actions"]
        }
        self.assertEqual(summer_policies[policy_ids["ac"]], "ON")
        self.assertEqual(summer_policies[policy_ids["ac_second"]], "OFF")
        self.assertEqual(summer_policies[policy_ids["fan"]], "ON")
        self.assertEqual(summer_policies[policy_ids["heater"]], "OFF")
        self.assertEqual(summer_policies[policy_ids["rgb"]], "KEEP_CURRENT")
        self.assertEqual(summer_policies[policy_ids["standard"]], "KEEP_CURRENT")
        summer_room_result = self.request(
            "POST",
            "/api/modes/apply",
            {
                "previewId": summer_room_preview["previewId"],
                "confirmed": True,
            },
        )
        self.assertEqual(summer_room_result["outcome"], "applied")
        for device_id, expected in (
            (policy_ids["ac"], "ON"),
            (policy_ids["ac_second"], "OFF"),
            (policy_ids["fan"], "ON"),
            (policy_ids["heater"], "OFF"),
            (policy_ids["rgb"], "OFF"),
            (policy_ids["standard"], "ON"),
        ):
            self.assertEqual(
                self.request("GET", f"/api/devices/{device_id}")["status"],
                expected,
            )
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['ac']}")["setting"],
            24,
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['fan']}")["setting"],
            2,
        )
        snapshot_db = sqlite3.connect(
            f"file:{self.database_dir / 'smarthome.db'}?mode=ro", uri=True
        )
        snapshot_rows = json.loads(
            snapshot_db.execute(
                "SELECT snapshot_json FROM mode_history "
                "ORDER BY id DESC LIMIT 1"
            ).fetchone()[0]
        )
        snapshot_db.close()
        self.assertEqual(
            {item["deviceId"] for item in snapshot_rows},
            set(policy_ids.values()),
        )
        self.assertTrue(any(
            item["deviceId"] == policy_ids["standard"] and
            item["property"] == "power" and
            item["value"] is True
            for item in snapshot_rows
        ))
        self.assertTrue(any(
            item["deviceId"] == policy_ids["ac"] and
            item["property"] == "setting" and
            item["value"] == 29
            for item in snapshot_rows
        ))
        self.restart_server()
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['ac']}")["status"],
            "ON",
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['ac_second']}")["status"],
            "OFF",
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['fan']}")["status"],
            "ON",
        )

        monsoon_preview = self.request(
            "POST",
            "/api/modes/preview",
            {
                "seasonId": "monsoon",
                "moodId": "",
                "roomNames": [policy_room],
                "deviceIds": [],
            },
        )
        monsoon_policies = {
            action["deviceId"]: action["policy"]
            for action in monsoon_preview["actions"]
        }
        self.assertEqual(
            monsoon_policies[policy_ids["ac"]], "CONFIGURE_ONLY"
        )
        self.assertEqual(
            monsoon_policies[policy_ids["heater"]], "KEEP_CURRENT"
        )
        monsoon_result = self.request(
            "POST",
            "/api/modes/apply",
            {"previewId": monsoon_preview["previewId"], "confirmed": True},
        )
        self.assertEqual(monsoon_result["outcome"], "applied")
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['ac']}")["status"],
            "ON",
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['ac']}")["setting"],
            25,
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['ac_second']}")["status"],
            "OFF",
        )

        winter_room_preview = self.request(
            "POST",
            "/api/modes/preview",
            {
                "seasonId": "winter",
                "moodId": "",
                "roomNames": [policy_room],
                "deviceIds": [],
            },
        )
        winter_policies = {
            action["deviceId"]: action["policy"]
            for action in winter_room_preview["actions"]
        }
        self.assertEqual(winter_policies[policy_ids["ac"]], "OFF")
        self.assertEqual(winter_policies[policy_ids["heater"]], "ON")
        self.assertEqual(winter_policies[policy_ids["fan"]], "CONFIGURE_ONLY")
        winter_room_result = self.request(
            "POST",
            "/api/modes/apply",
            {
                "previewId": winter_room_preview["previewId"],
                "confirmed": True,
            },
        )
        self.assertEqual(winter_room_result["outcome"], "applied")
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['ac']}")["status"],
            "OFF",
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['heater']}")["status"],
            "ON",
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['fan']}")["status"],
            "ON",
        )
        room_restore = self.request(
            "POST",
            "/api/modes/restore",
            {"confirmed": True, "roomName": policy_room},
        )
        self.assertTrue(room_restore["success"])
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['ac']}")["status"],
            "ON",
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['heater']}")["status"],
            "OFF",
        )
        spring_preview = self.request(
            "POST",
            "/api/modes/preview",
            {
                "seasonId": "spring",
                "moodId": "",
                "roomNames": [policy_room],
                "deviceIds": [],
            },
        )
        spring_result = self.request(
            "POST",
            "/api/modes/apply",
            {"previewId": spring_preview["previewId"], "confirmed": True},
        )
        self.assertEqual(spring_result["outcome"], "applied")
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['ac']}")["status"],
            "OFF",
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{policy_ids['heater']}")["status"],
            "OFF",
        )

        study_policy_room = f"Mode Study Policy {suffix}"
        self.request("POST", "/api/rooms", {"name": study_policy_room}, 201)
        study_policy_ids = {
            "rgb": f"FOCUS_RGB_{suffix}",
            "tunable": f"FOCUS_TUNABLE_{suffix}",
            "standard": f"FOCUS_STANDARD_{suffix}",
            "fan": f"FOCUS_FAN_{suffix}",
        }
        for device_id, device_type, extra in (
            (study_policy_ids["rgb"], "Light", {
                "lightType": "rgb", "initialOn": False,
            }),
            (study_policy_ids["tunable"], "Light", {
                "lightType": "tunable-white", "initialOn": False,
            }),
            (study_policy_ids["standard"], "Light", {"initialOn": False}),
            (study_policy_ids["fan"], "Fan", {}),
        ):
            self.request(
                "POST",
                "/api/devices",
                {
                    "id": device_id,
                    "name": device_id,
                    "type": device_type,
                    "roomName": study_policy_room,
                    "powerWatts": 60,
                    "temporary": False,
                    **extra,
                },
                201,
            )
        focus_fan_before = self.request(
            "GET", f"/api/devices/{study_policy_ids['fan']}"
        )["status"]
        focus_preview = self.request(
            "POST",
            "/api/modes/preview",
            {
                "seasonId": "",
                "moodId": "focus",
                "roomNames": [study_policy_room],
                "deviceIds": [],
            },
        )
        self.assertEqual(
            {action["deviceId"] for action in focus_preview["actions"]},
            set(study_policy_ids.values()),
        )
        focus_result = self.request(
            "POST",
            "/api/modes/apply",
            {"previewId": focus_preview["previewId"], "confirmed": True},
        )
        self.assertEqual(focus_result["outcome"], "partial")
        for device_id in (
            study_policy_ids["rgb"],
            study_policy_ids["tunable"],
            study_policy_ids["standard"],
        ):
            self.assertEqual(
                self.request("GET", f"/api/devices/{device_id}")["status"],
                "ON",
            )
        self.assertEqual(
            self.request("GET", f"/api/devices/{study_policy_ids['fan']}")["status"],
            "ON",
        )
        self.assertEqual(focus_fan_before, "OFF")
        self.assertTrue(any(
            action["deviceId"] == study_policy_ids["fan"] and
            action["property"] == "power" and
            action["value"] is True and
            action["status"] == "applied"
            for action in focus_result["actions"]
        ))

        movie_preview = self.request(
            "POST",
            "/api/modes/preview",
            {
                "seasonId": "",
                "moodId": "movie-night",
                "roomNames": [study_policy_room],
                "deviceIds": [],
            },
        )
        movie_policies = {
            action["deviceId"]: action["policy"]
            for action in movie_preview["actions"]
        }
        self.assertEqual(movie_policies[study_policy_ids["rgb"]], "ON")
        self.assertEqual(movie_policies[study_policy_ids["standard"]], "KEEP_CURRENT")
        movie_room_result = self.request(
            "POST",
            "/api/modes/apply",
            {"previewId": movie_preview["previewId"], "confirmed": True},
        )
        self.assertEqual(movie_room_result["outcome"], "applied")
        self.assertEqual(
            self.request("GET", f"/api/devices/{study_policy_ids['standard']}")["status"],
            "ON",
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{study_policy_ids['rgb']}")["brightness"],
            15,
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{study_policy_ids['fan']}")["status"],
            "ON",
        )

        sleep_room = f"Mode Bedroom Sleep {suffix}"
        self.request("POST", "/api/rooms", {"name": sleep_room}, 201)
        sleep_light_id = f"SLEEP_LIGHT_{suffix}"
        sleep_fan_id = f"SLEEP_FAN_{suffix}"
        self.request(
            "POST",
            "/api/devices",
            {
                "id": sleep_light_id,
                "name": sleep_light_id,
                "type": "Light",
                "roomName": sleep_room,
                "powerWatts": 8,
                "temporary": False,
                "initialOn": True,
                "lightType": "rgb",
            },
            201,
        )
        self.request(
            "POST",
            "/api/devices",
            {
                "id": sleep_fan_id,
                "name": sleep_fan_id,
                "type": "Fan",
                "roomName": sleep_room,
                "powerWatts": 50,
                "temporary": False,
                "initialOn": False,
            },
            201,
        )
        sleep_preview = self.request(
            "POST",
            "/api/modes/preview",
            {
                "seasonId": "",
                "moodId": "sleep",
                "roomNames": [sleep_room],
                "deviceIds": [],
            },
        )
        sleep_result = self.request(
            "POST",
            "/api/modes/apply",
            {"previewId": sleep_preview["previewId"], "confirmed": True},
        )
        self.assertEqual(sleep_result["outcome"], "applied")
        self.assertEqual(
            self.request("GET", f"/api/devices/{sleep_light_id}")["status"],
            "OFF",
        )
        self.assertEqual(
            self.request("GET", f"/api/devices/{sleep_fan_id}")["status"],
            "ON",
        )
        self.assertTrue(any(
            action["deviceId"] == sleep_fan_id and
            action["property"] == "power" and
            action["value"] is True
            and action["status"] == "applied"
            for action in sleep_result["actions"]
        ))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0]], verbosity=2)
