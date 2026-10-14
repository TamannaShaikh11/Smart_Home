
import json
import urllib.request
import urllib.error

BASE_URL = "http://localhost:8080/api/devices"
DEVICE_ID = "TEST_DELETE_CHECK_01"

payload = {
    "id": DEVICE_ID,
    "name": "Delete Check Device",
    "type": "Air Purifier",
    "roomName": "Living Room",
    "powerWatts": 45,
    "temporary": True,
    "lifetimeSeconds": 600
}

def send_request(url, method, body=None):
    data = json.dumps(body).encode("utf-8") if body else None
    request = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/json"},
        method=method
    )

    try:
        with urllib.request.urlopen(request, timeout=10) as response:
            print(f"{method} -> HTTP {response.status}")
            print(response.read().decode("utf-8"))
            return response.status
    except urllib.error.HTTPError as error:
        print(f"{method} -> HTTP {error.code}")
        print(error.read().decode("utf-8"))
        return error.code

print("1. Creating device...")
created = send_request(BASE_URL, "POST", payload)

if created in (200, 201):
    print("\n2. Deleting device immediately...")
    send_request(f"{BASE_URL}/{DEVICE_ID}", "DELETE")
else:
    print("\nCreation failed; skipping deletion.")
