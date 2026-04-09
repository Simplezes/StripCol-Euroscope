# StripCol - Euroscope

EuroScope plugin that syncs your ATC session in real time over WebSocket. It connects to a StripCol gateway and keeps other clients (e.g. a strip board app) in sync with your position.


---

## Commands

| Command | What it does |
|---|---|
| `.stripset <IP>` | Point the plugin at a different gateway (defaults to `127.0.0.1`). |
| `.striprestart` | Reconnect to the gateway. |
| `.stripcode` | Show your current pairing code. |
| `.stripclearance` | Turn the Clearance & Squawk feature on or off. |

---

## Pairing Code

Once connected, the plugin generates a short code like `A7X9Q`. Share this with anyone who needs to join your session they enter it in their StripCol client to sync up with you.

---

## Network Setup

- **Same machine or local network:** the default `127.0.0.1` works out of the box, or use your local IP (`192.168.x.x`).
- **Over the internet:** set your public IP with `.stripset`, forward port **3000** on your router, and allow the plugin through Windows Firewall.

---

## Building from Source

> Only needed if you want to build the DLL yourself instead of using a release.

**You'll need:**
- Windows (x64)
- Visual Studio 2022 with the **Desktop development with C++** workload

**Steps:**
1. Open `StripCol.sln` in Visual Studio 2022.
2. Select the **Release | x64** configuration.
3. Press `Ctrl+Shift+B`. The output `StripCol.dll` will be in `x64/Release/`.

> If you plan to use the **Clearance & Squawk** feature, also copy `common/Secrets.h.example` to `common/Secrets.h` and fill in your server details before building.

---

## License

MIT - see [LICENSE.txt](LICENSE.txt).
