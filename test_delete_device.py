
import urllib.request
import urllib.error

url = "http://localhost:8080/api/devices/TEST_PURIFIER_01"

request = urllib.request.Request(
    url,
    method="DELETE"
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
