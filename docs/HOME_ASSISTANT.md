# Home Assistant and MQTT broker on a Raspberry Pi — setup notes

How the Raspberry Pi 5 receives this node's MQTT data: Docker, a Mosquitto
broker, and Home Assistant (HA) for evaluation. Written as a step-by-step
reference, so the setup can be repeated or repaired later.

Set up and verified on 2026-10-08 with:

| Component | Version |
|---|---|
| Raspberry Pi 5, 8 GB | Raspberry Pi OS 64-bit (Debian 12 bookworm, `arm64`), kernel `6.12 rpi-2712` |
| Docker Engine / Compose | 29.8.2 / v5.6.0, from Docker's own apt repository |
| Home Assistant | 2026.10.0, Container installation (`ghcr.io/home-assistant/home-assistant:stable`) |
| Mosquitto | 2.1.2 (`eclipse-mosquitto:2`) |

Based on the official guide
<https://www.home-assistant.io/installation/raspberrypi-other> (Container
section), with the corrections listed in [section 11](#11-pitfalls-and-troubleshooting).


## 1. Overview

```
ESP32 pressure monitor ──MQTT──▶ Mosquitto broker ──MQTT──▶ Home Assistant
192.168.8.127                    Pi, port 1883              Pi, port 8123
publishes once per second        ~/mosquitto                ~/homeassistant
```

| Item | Value |
|---|---|
| Pi | `rainer@192.168.8.135` (hostname `pi6`, WLAN `wlan0`); Ethernet `eth0` is a different network, `192.168.0.206` |
| ESP32 | `192.168.8.127`, WLAN `HUAWEI-B311-7F94` (same network as the Pi's `wlan0`) |
| MQTT topic | `pressure_monitor/measurement` |
| Payload | `{"pressure":2.31,"min":2.29,"max":2.35}` — bar, mean/min/max over the publish interval |
| HA web interface | <http://192.168.8.135:8123> |

The broker and HA are **two independent Docker Compose projects**. The broker is
the core of the sensor network and must keep running whether HA is used or not;
HA is just one of its clients.


## 2. Docker in four terms

- **Image** — a read-only package with a program and everything it needs.
  Downloaded ("pulled") from a registry.
- **Container** — a running instance of an image. Isolated, but shares the
  Pi's kernel, so it is lightweight. Containers are disposable: delete one and
  create a new one from the same image at any time.
- **Bind mount** — a folder on the Pi that is visible inside the container.
  Everything that must survive (configuration, database) lives there, never in
  the container itself.
- **Docker Compose** — describes containers in a `compose.yaml` file instead of
  a long `docker run` command. A folder with a `compose.yaml` is a **project**,
  named after the folder; `docker compose …` acts only on the project in the
  current folder (or the one given with `-f <path>/compose.yaml`).


## 3. Install Docker Engine

Debian's own `docker.io` package is often older than the Docker 23.0 HA
requires, so Docker is installed from Docker's official repository. `apt` then
keeps it up to date.

```bash
# Helper tools
sudo apt update
sudo apt install -y ca-certificates curl

# Docker's signing key, so apt can verify the packages
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/debian/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc

# Docker's package source (expands to arm64 / bookworm)
echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/debian $(. /etc/os-release && echo "$VERSION_CODENAME") stable" | sudo tee /etc/apt/sources.list.d/docker.list > /dev/null

# Docker Engine, CLI, container runtime and the Compose plugin
sudo apt update
sudo apt install -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
```

The Docker service starts immediately and on every boot.

Allow the user to run `docker` without `sudo`, then **log out and in again**
(group changes apply only to new logins):

```bash
sudo usermod -aG docker $USER
```

> Membership in the `docker` group is effectively root access to the Pi. Fine
> for your own account, not for untrusted ones.

Verify:

```bash
docker --version
docker compose version
docker run hello-world        # pulls a tiny image, prints "Hello from Docker!"
docker ps -a                  # the exited hello-world container, random name
docker rm <name>              # remove the test container ...
docker rmi hello-world        # ... then its image
```


## 4. Home Assistant container

### 4.1 Project folder

```bash
mkdir -p ~/homeassistant/config
cd ~/homeassistant
```

`~/homeassistant/config` holds all HA data. Together with `compose.yaml` it is
everything that needs backing up.

### 4.2 `~/homeassistant/compose.yaml`

```yaml
services:
  homeassistant:
    container_name: homeassistant
    image: ghcr.io/home-assistant/home-assistant:stable
    volumes:
      - ./config:/config
      - /etc/localtime:/etc/localtime:ro
      - /run/dbus:/run/dbus:ro
    environment:
      TZ: Europe/London
    restart: unless-stopped
    stop_grace_period: 60s
    privileged: true
    network_mode: host
```

| Line | Meaning |
|---|---|
| `container_name` | Fixed name instead of a random one. |
| `image: …:stable` | `stable` always points to the latest stable HA release. |
| `./config:/config` | HA's data folder, relative to `compose.yaml`. |
| `/etc/localtime:…:ro` | The Pi's clock settings, read-only. |
| `/run/dbus:/run/dbus:ro` | D-Bus access, needed for Bluetooth. |
| `TZ` | Time zone for the container's logs. HA's own time zone is set in its web interface. |
| `restart: unless-stopped` | Restart after crashes and reboots, unless stopped on purpose. |
| `stop_grace_period: 60s` | Time for HA to close its database cleanly on stop. |
| `privileged: true` | Full hardware access (USB sticks, Bluetooth). Weakens isolation; official recommendation. |
| `network_mode: host` | Uses the Pi's network directly, needed for device discovery (mDNS, UPnP). The web interface is on the Pi's port 8123. |

YAML indentation must be spaces, never tabs. Edit with `nano compose.yaml`
(save Ctrl+O, Enter; exit Ctrl+X).

### 4.3 Start

```bash
docker compose pull      # download the image (a few hundred MB; 3.4 GB unpacked)
docker compose up -d     # create and start the container in the background
docker compose ps        # status: "Up ..."
docker compose logs -f   # live log; Ctrl+C ends the display, HA keeps running
```

The first start takes 1–3 minutes. Normal log output: `s6-rc: info: service …`
lines (the process manager inside the container) and a harmless
`SyntaxWarning: 'return' in a 'finally' block` from the `rich` library.

The files in `config/` are owned by **root**, because the container runs as
root. Editing them needs `sudo`.


## 5. First start and onboarding

Open <http://192.168.8.135:8123> → "Welcome home!" screen. The answers are
stored in `config/.storage/` and can all be changed later under
Einstellungen → System → Zuhause-Informationen.

1. **Create my smart home** (not "Restore from backup").
2. **Owner account** — name, username, password. This administrator account is
   local to the Pi; there is no cloud account. Keep the password safe:
   resetting it on a Container install needs the command line.
3. **Home location**, country, language, time zone, units, currency. The
   location drives sunrise/sunset, weather and presence; it stays on the Pi.
4. **Analytics** — optional, anonymous, everything off by default.
5. **Discovered devices** — what HA found on the LAN by itself. Can be skipped.

Main menu (the HA interface here is German):

| Menu | Content |
|---|---|
| Übersicht (Overview) | Dashboard, generated automatically at first |
| Landkarte / Aktivität / Verlauf | Map, event log, history graphs |
| Energie | Energy monitoring |
| Einstellungen → Geräte & Dienste | Integrations: where devices and services are added |
| Einstellungen → Automationen & Szenen | Rules: trigger, condition, action |
| Einstellungen → Werkzeuge | Developer tools (YAML check, restart, states, templates) |

A Container installation has **no apps/add-ons**. What HA OS installs as an
add-on (Mosquitto, Zigbee2MQTT, ESPHome, …) runs as a separate container here.

To try HA without hardware: Einstellungen → Geräte & Dienste → Integration
hinzufügen → **Demo** adds simulated devices; delete the integration
afterwards.


## 6. Mosquitto MQTT broker

The broker is its own Compose project in `~/mosquitto`, deliberately
independent of HA: it starts at boot on its own, `docker compose down` in
`~/homeassistant` never touches it, and removing HA leaves it running.

### 6.1 Folders and `~/mosquitto/config/mosquitto.conf`

```bash
mkdir -p ~/mosquitto/config ~/mosquitto/data
cd ~/mosquitto
```

```
listener 1883
allow_anonymous true
persistence true
persistence_location /mosquitto/data/
log_dest stdout
```

| Line | Meaning |
|---|---|
| `listener 1883` | Accept connections on the standard MQTT port. **Without it, Mosquitto 2 accepts connections only from itself.** |
| `allow_anonymous true` | No login. Acceptable for a first test on the home network only — see [section 12](#12-open-items). |
| `persistence …` | Retained messages and sessions survive restarts. |
| `log_dest stdout` | Log goes to `docker compose logs`. |

On first start the container makes these files belong to its own user; later
edits need `sudo nano …`.

### 6.2 `~/mosquitto/compose.yaml`

```yaml
services:
  mosquitto:
    container_name: mosquitto
    image: eclipse-mosquitto:2
    volumes:
      - ./config:/mosquitto/config
      - ./data:/mosquitto/data
    ports:
      - "1883:1883"
    restart: unless-stopped
```

`:2` follows the latest 2.x release without jumping to a future major version.
Unlike HA, Mosquitto needs no host networking: it runs in Docker's own isolated
network, and `ports: "1883:1883"` opens exactly one port (Pi side left,
container side right), on all of the Pi's interfaces and on `localhost`.

### 6.3 Start and test

```bash
docker compose up -d
docker compose logs     # "Opening ipv4 listen socket on port 1883", "mosquitto version 2.x running"
docker ps               # homeassistant and mosquitto, independent of each other
```

Watch all messages of this node (works from any folder, uses the container
name; Ctrl+C to stop):

```bash
docker exec -it mosquitto mosquitto_sub -t 'pressure_monitor/#' -v
```

`#` is the topic wildcard, `-v` prints the topic with each message. For a quick
non-interactive check, `-C 3 -W 10` stops after 3 messages or 10 s.


## 7. Connect the ESP32

The broker address is a build flag in [`../platformio.ini`](../platformio.ini):

```ini
    -DMQTT_BROKER_HOST=\"192.168.8.135\"
```

It must be the Pi's address **on the network the ESP32 is in** (here the Pi's
WLAN address). The value used before was `192.168.0.200`; this flag decides
which network the board works on.

```bash
pio run --target upload
pio device monitor
```

Expected: `MQTT connected to 192.168.8.135:1883, topic 'pressure_monitor/measurement'`,
and one JSON line per second in the `mosquitto_sub` window.

`MQTT connection to … failed, state=-2` means the TCP connection could not be
opened: wrong address, unreachable network, or the broker is not running.


## 8. Home Assistant: MQTT integration and sensors

### 8.1 MQTT integration

Einstellungen → Geräte & Dienste → Integration hinzufügen → search **MQTT** →
the plain entry **MQTT**:

- Broker: `localhost` (HA uses the Pi's network; the broker's port is open on `localhost`)
- Port: `1883`
- Username / password: empty

Test: the MQTT integration → ⋮ → Konfigurieren → listen to topic
`pressure_monitor/#`.

### 8.2 Sensors in `configuration.yaml`

The firmware sends no HA discovery messages, so the sensors are declared
manually. Append to `~/homeassistant/config/configuration.yaml` (root-owned):

```bash
cd ~/homeassistant
sudo nano config/configuration.yaml
```

```yaml
mqtt:
  sensor:
    - name: "Pressure"
      unique_id: pressure_monitor_pressure
      state_topic: "pressure_monitor/measurement"
      value_template: "{{ value_json.pressure }}"
      unit_of_measurement: "bar"
      device_class: pressure
      state_class: measurement
      device:
        identifiers: ["pressure_monitor"]
        name: "Pressure monitor"
    - name: "Pressure min"
      unique_id: pressure_monitor_min
      state_topic: "pressure_monitor/measurement"
      value_template: "{{ value_json.min }}"
      unit_of_measurement: "bar"
      device_class: pressure
      state_class: measurement
      device:
        identifiers: ["pressure_monitor"]
        name: "Pressure monitor"
    - name: "Pressure max"
      unique_id: pressure_monitor_max
      state_topic: "pressure_monitor/measurement"
      value_template: "{{ value_json.max }}"
      unit_of_measurement: "bar"
      device_class: pressure
      state_class: measurement
      device:
        identifiers: ["pressure_monitor"]
        name: "Pressure monitor"
```

| Key | Meaning |
|---|---|
| `state_topic` | Topic to subscribe to. |
| `value_template` | Picks one field from the JSON (`value_json` is the parsed payload). |
| `unique_id` | Permanent ID; allows renaming and customizing in the web interface. |
| `device_class: pressure` | Pressure icon and unit conversion (e.g. psi). |
| `state_class: measurement` | Enables **long-term statistics**: 5-minute and hourly mean/min/max, kept indefinitely. Detailed history is purged after 10 days by default. |
| `device` | Groups the three sensors as one device, "Pressure monitor". |

### 8.3 Check and restart

In the web interface: Einstellungen → **Werkzeuge** → tab **YAML** → section
"Überprüfen und neu starten" → **Konfiguration prüfen**. Expected: "Die
Konfiguration verhindert nicht den Start von Home Assistant!". Then, in the
same tab under "Home Assistant", **Neu starten**. If the Werkzeuge tile is
missing, enable advanced mode in the user profile (click the user name at the
bottom of the sidebar).

The same from the command line, also when the web interface does not come up:

```bash
docker exec homeassistant python -m homeassistant --script check_config --config /config
cd ~/homeassistant && docker compose restart
```

`check_config` printing only `Testing configuration at /config` means no errors.

Result: Einstellungen → Geräte & Dienste → MQTT → device **Pressure monitor**
with the sensors *Pressure*, *Pressure min*, *Pressure max*.


## 9. Day-to-day operation

Commands apply to the project in the current folder (`~/homeassistant` or
`~/mosquitto`), or to `-f <folder>/compose.yaml`. Plain `docker ps` lists all
containers on the Pi.

### 9.1 Status and logs

```bash
docker compose ps
docker compose logs --tail 50
docker compose logs -f
```

HA also shows its log under Einstellungen → System → Protokolle, which is
easier to read.

### 9.2 Start, stop, apply changes

| Command | Effect | Container afterwards |
|---|---|---|
| `docker compose stop` | Clean shutdown (HA: up to 60 s) | Exists, stopped; stays stopped after reboot |
| `docker compose start` | Start the stopped container | Same container |
| `docker compose restart` | `stop` + `start` | Same container |
| `docker compose down` | Stop **and remove** the container | Gone; data in the bind mounts stays |
| `docker compose up -d` | Create/start; **recreates** the container if `compose.yaml` or the image changed | New or unchanged |

A container's settings cannot be changed in place. After editing
`compose.yaml`, `docker compose up -d` throws the old container away and
creates a new one; the data folders are untouched.

Changes to HA's `configuration.yaml` need only an HA restart (web interface or
`docker compose restart`), not `up -d`.

### 9.3 Update

```bash
docker compose pull       # new image; the old container keeps running
docker compose up -d      # recreate with the new image (about 1 min downtime)
docker image prune -f     # delete the old image, otherwise ~3 GB pile up per HA update
```

HA cannot update itself in a Container installation. It releases monthly
(first Wednesday) plus bugfix releases; read "Backward-incompatible changes" in
the release notes on home-assistant.io/blog before updating.

To control the version, replace `:stable` with a fixed tag, e.g.
`ghcr.io/home-assistant/home-assistant:2026.10.0`. Rolling back works the same
way, but a database migrated by a newer version may not work with an older
one — back up first.

### 9.4 Backup and restore

Both projects are plain folders. Stop the containers so the HA database is not
written during the copy:

```bash
docker compose -f ~/homeassistant/compose.yaml stop
docker compose -f ~/mosquitto/compose.yaml stop
sudo tar czf ~/pi-backup-$(date +%F).tar.gz -C ~ homeassistant mosquitto
docker compose -f ~/mosquitto/compose.yaml start
docker compose -f ~/homeassistant/compose.yaml start
```

`sudo` is needed for the root-owned files. Copy the archive off the Pi (run on
the Mac):

```bash
scp rainer@192.168.8.135:pi-backup-*.tar.gz ~/Downloads/
```

Restore (example: HA only; the broker works the same way):

```bash
docker compose -f ~/homeassistant/compose.yaml down
sudo mv ~/homeassistant ~/homeassistant.old
sudo tar xzf ~/pi-backup-2026-10-08.tar.gz -C ~ homeassistant
docker compose -f ~/homeassistant/compose.yaml up -d
```

HA's built-in backups (Einstellungen → System → Backups) also work on a
Container installation. They are stored in `config/backups/`, i.e. on the same
SD card, so copy them elsewhere or configure a network share as the location.

### 9.5 Shell inside a container

```bash
docker compose exec homeassistant bash     # in ~/homeassistant; leave with exit
```

Everything changed inside a container outside its bind mounts is lost when the
container is recreated. Look around, do not install anything there.

### 9.6 Disk usage

```bash
docker system df       # space used by images and containers
docker system prune    # delete stopped containers, unused networks, dangling images (asks first)
```

Bind-mounted data folders are never touched by Docker's cleanup commands.


## 10. Removing everything

These steps cannot be undone; back up first if the data might be needed later.

Remove only Home Assistant (broker and Docker stay):

```bash
docker compose -f ~/homeassistant/compose.yaml down
docker rmi ghcr.io/home-assistant/home-assistant:stable
sudo rm -rf ~/homeassistant
```

Remove only the broker:

```bash
docker compose -f ~/mosquitto/compose.yaml down
docker rmi eclipse-mosquitto:2
sudo rm -rf ~/mosquitto
```

Remove Docker itself (this also stops and deletes **all** containers and
images, including the broker):

```bash
sudo apt purge -y docker-ce docker-ce-cli containerd.io docker-buildx-plugin docker-compose-plugin
sudo rm -rf /var/lib/docker /var/lib/containerd
sudo rm /etc/apt/sources.list.d/docker.list /etc/apt/keyrings/docker.asc
sudo gpasswd -d rainer docker
```

`apt purge` leaves `/var/lib/docker` and `/var/lib/containerd` (images and
containers) behind; they are deleted by hand.


## 11. Pitfalls and troubleshooting

| Symptom / topic | Explanation |
|---|---|
| HA guide recommends `DISABLE_JEMALLOC=true` for kernels with pages larger than 4 KB (the Pi 5 kernel uses 16 KB, `getconf PAGESIZE` = 16384) | **Obsolete.** Current HA uses mimalloc instead of jemalloc and logs `DISABLE_JEMALLOC is set but no longer has any effect`. Leave it out. |
| Mosquitto runs, but no client can connect | `listener 1883` missing in `mosquitto.conf`; Mosquitto 2 then listens only locally. |
| ESP32: `MQTT connection to … failed, state=-2` | Broker address not reachable from the ESP32's network, or broker not running. Check `MQTT_BROKER_HOST` and `docker compose -f ~/mosquitto/compose.yaml ps`. |
| "Developer tools" not in the sidebar | In HA 2026.10 they are under Einstellungen → **Werkzeuge**. If the tile is missing, enable advanced mode in the user profile. |
| `Permission denied` editing files in `config/` or `mosquitto/` | Files are owned by the container's user (root, or Mosquitto's own); use `sudo`. |
| `docker compose logs -f` seems to hang | It follows the log and waits for new lines. Ctrl+C ends the display only. |
| `docker compose …` says "no configuration file provided" | Wrong folder. `cd` into the project folder or use `-f <folder>/compose.yaml`. |
| `docker stats` shows memory `0B / 0B` | Raspberry Pi OS disables the memory cgroup controller by default. Only affects statistics, not HA. |
| HA sensors show exactly `0` | Data path works; 0 V on AIN0 with the default calibration (0 V → 0 bar). Check the sensor and ADS1115 wiring and the serial boot log. |
| Broker reachable from HA but logged as `172.18.0.1` | Normal: HA connects via `localhost`, which arrives through the gateway of the broker project's Docker network. |


## 12. Open items

- **Secure the broker**: a Mosquitto password file and `allow_anonymous false`,
  plus `MQTT_USERNAME` / `MQTT_PASSWORD` in `platformio.ini` and the
  credentials in HA's MQTT integration. Today anyone on the WLAN can read and
  publish.
- **MQTT discovery in the firmware**: publish retained config messages to
  `homeassistant/sensor/…/config` at boot, plus an availability topic with a
  last-will message. Makes the YAML in section 8.2 unnecessary and lets HA show
  the node as unavailable when it goes offline.
- **Fixed address for the Pi**: a DHCP reservation for `192.168.8.135` in the
  Huawei router, so the ESP32 always finds the broker.
- **Publish period**: 3 values per second is about 260,000 history entries per
  day in HA. If HA does not need 1 s resolution, `MQTT_PUBLISH_PERIOD_MS=10000`
  is plenty; XCP covers fast measurements.
- **More nodes**: each board needs its own `MQTT_TOPIC` and its own sensor
  block (client IDs are already unique, derived from the chip ID).


## Cheat sheet

| Task | Command |
|---|---|
| All containers | `docker ps` |
| Status / log of a project | `docker compose ps` / `docker compose logs --tail 50` (`-f` to follow) |
| Stop / start / restart | `docker compose stop` / `start` / `restart` |
| Apply `compose.yaml` changes | `docker compose up -d` |
| Update | `docker compose pull` → `docker compose up -d` → `docker image prune -f` |
| Watch MQTT traffic | `docker exec -it mosquitto mosquitto_sub -t 'pressure_monitor/#' -v` |
| Check HA configuration | `docker exec homeassistant python -m homeassistant --script check_config --config /config` |
| Shell in HA container | `docker compose exec homeassistant bash` |
