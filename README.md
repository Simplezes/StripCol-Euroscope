# StripCol – EuroScope Plugin

StripCol is a real-time synchronization plugin for EuroScope that connects your position to a StripCol WebSocket gateway and allows external applications (or other StripCol clients) to:

* Sync assumed aircraft
* Update assigned data (ALT, HDG, SPD, MACH, SQUAWK, etc.)
* Accept / refuse / initiate handoffs
* Push SID/STAR amendments
* Share ATC list
* Retrieve nearby aircraft
* Synchronize state after reconnect

The plugin communicates with a StripCol WebSocket server running on port **3000**.

---

# Features

## 🔌 WebSocket Gateway Connection

* Connects to configurable gateway IP
* Automatic reconnect
* Heartbeat (ping every 30s)
* Safe non-blocking shutdown
* No UI freezing

## 🛩 Aircraft Synchronization

* Sends aircraft data when assumed
* Tracks:

  * Cleared altitude
  * Final altitude
  * Assigned heading
  * Assigned speed
  * Assigned Mach
  * Squawk
  * Direct-to
  * SID / STAR
  * Route
  * Remarks
  * Coordination points
* Sends release when tracking ends

## 🔄 Live Updates

* Detects flight plan changes
* Sends `fpupdate` only when data changes
* Sync command re-pushes:

  * Registration
  * ATC list
  * All assumed aircraft

## 🤝 Handoff & Transfer Support

Handles:

* Accept handoff
* Refuse handoff
* Initiate handoff
* Transfer to me detection

## 📡 Nearby Aircraft Query

Returns aircraft within 50NM of controller position.

---

# Installation

1. Build the DLL.
2. Place it in your EuroScope plugins folder.
3. Load it inside EuroScope.
4. Start your StripCol gateway server (port 3000).

---

# How It Works

StripCol acts as a **WebSocket client**.

Flow:

1. EuroScope position connect → plugin connects to gateway.
2. Sends a registration JSON containing:

   * Pairing code
   * Callsign
   * Controller name
   * Facility
   * Rating
   * Frequency
3. Gateway manages multi-client sync.

---

# Commands

## `.striprestart`

Restarts the gateway connection.

```
.striprestart
```

---

## `.stripset <IP>`

Sets the gateway IP address.

```
.stripset 192.168.1.15
```

If no IP is provided, defaults to:

```
127.0.0.1
```

After setting, the plugin reconnects automatically.

---

## `.stripcode`

Displays the current pairing code.

```
.stripcode
```

---

# Pairing Code

On first successful registration, a 5-character pairing code is generated:

Example:

```
StripCol Pairing Code: A7X9Q
```

This code is used by other StripCol clients to join your session.

---

# Network Requirements

* Gateway must be running on port **3000**
* If using LAN:

  * Use local IPv4 (e.g. `192.168.x.x` or `10.x.x.x`)
* If using internet:

  * Use public IP
  * Configure port forwarding
* Allow StripCol through Windows Firewall

---

# JSON Messages

## Sent to Gateway

* `register`
* `ping`
* `aircraft`
* `fpupdate`
* `release`
* `transfer`
* `atclist`
* `nearby-aircraft`

## Received from Gateway

* `set-cleared-alt`
* `set-final-alt`
* `set-assigned-heading`
* `set-assigned-speed`
* `set-assigned-mach`
* `set-squawk`
* `set-departureTime`
* `set-direct-point`
* `set-sid`
* `set-star`
* `accept-handoff`
* `refuse-handoff`
* `ATC-transfer`
* `assume-aircraft`
* `end-tracking`
* `sync`
* `get-nearby-aircraft`

---

# Threading Model

* Dedicated WebSocket worker thread
* UI thread never blocks
* Safe shutdown using atomic flags
* Socket protected by mutex
* Aircraft state protected by mutex

This prevents freezing inside EuroScope.

---

# Architecture Overview

```
EuroScope
   ↓
StripCol Plugin (WebSocket Client)
   ↓
StripCol Gateway (Port 3000)
   ↓
Other StripCol Clients
```

---