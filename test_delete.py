import urllib.request
import urllib.error
import json

base_url = "http://localhost:8080/api/devices"

device = {
    "id": "TEST_DELETE_NEW_01",
    "name": "Delete Test Device",
    "type": "Air Purifier",
    "roomName": "Living Room",
    "powerWatts": 45,
    "temporary": True,
    "lifetimeSeconds": 600
}

def request_api(url, method, data=None):
    body = json.dumps(data).encode() if data is not None else None

    req = urllib.request.Request(
        url,
        data=body,
        method=method,
        headers={"Content-Type": "application/json"}
    )

    try:
        with urllib.request.urlopen(req) as response:
            print(method, "Status:", response.status)
            print("Response:", response.read().decode())
    except urllib.error.HTTPError as error:
        print(method, "Status:", error.code)
        print("Response:", error.read().decode())

request_api(base_url, "POST", device)
request_api(base_url + "/TEST_DELETE_NEW_01", "DELETE")
request_api(base_url, "GET")
