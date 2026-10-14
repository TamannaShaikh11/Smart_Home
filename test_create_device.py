
import json
import urllib.request
import urllib.error

url = "http://localhost:8080/api/devices"

payload = {
    "id": "TEST_PURIFIER_01",
    "name": "Test Purifier",
    "type": "Air Purifier",
    "roomName": "Living Room",
    "powerWatts": 45,
    "temporary": True,
    "lifetimeSeconds": 120
}

data = json.dumps(payload).encode("utf-8")

request = urllib.request.Request(
    url,
    data=data,
    headers={"Content-Type": "application/json"},
    method="POST"
)

try:
    with urllib.request.urlopen(request, timeout=10) as response:
        print("HTTP Status:", response.status)
        print("Response Body:")
        print(response.read().decode("utf-8"))

except urllib.error.HTTPError as error:
    print("HTTP Status:", error.code)
    print("Response Body:")
    print(error.read().decode("utf-8"))

except urllib.error.URLError as error:
    print("Connection Error:", error.reason)
    print("Is SmartHomeAPI.exe running on port 8080?")
