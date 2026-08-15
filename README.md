# ESPHome Components

A collection of custom [ESPHome](https://esphome.io) components.

| Component | Platform | Description |
| --- | --- | --- |
| [`comfoair`](#comfoair) | `climate` (+ sensors) | Control and monitoring of Zehnder ComfoAir / StorkAir WHR compatible ventilation units over the serial CC-Ease protocol |
| [`current_tilt_based`](#current_tilt_based) | `cover` | Time based cover/venetian blind with motor current feedback for end position, obstacle and malfunction detection |

## Installation

Add the repository as an [external component](https://esphome.io/components/external_components.html) to your device YAML:

```yaml
external_components:
  - source: github://retomarek/esphome-components
    components: [comfoair, current_tilt_based]
```

To pin a specific revision (recommended for stable setups):

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/retomarek/esphome-components
      ref: main
    components: [comfoair]
```

---

## comfoair

Talks to a ComfoAir ventilation unit over the RS232 CC-Ease connector and exposes it as a
`climate` entity plus a large set of optional sensors.

### Hardware

* UART at **9600 baud, 8N1**
* The unit uses RS232 levels, so a level shifter (e.g. MAX3232) between the ESP and the
  CC-Ease / RS232 connector of the ventilation unit is required.
* The component polls the unit every 600 ms and cycles through the different data requests.

### Configuration

```yaml
uart:
  id: uart_bus
  tx_pin: GPIO17
  rx_pin: GPIO16
  baud_rate: 9600

comfoair:
  id: comfoair1
  name: "Ventilation"
  uart_id: uart_bus

  # optional sensors – add only the ones you need
  outside_air_temperature:
    name: "Outside Air Temperature"
  supply_air_temperature:
    name: "Supply Air Temperature"
  return_air_temperature:
    name: "Return Air Temperature"
  exhaust_air_temperature:
    name: "Exhaust Air Temperature"
  intake_fan_speed:
    name: "Intake Fan Speed"
  exhaust_fan_speed:
    name: "Exhaust Fan Speed"
  ventilation_level:
    name: "Ventilation Level"
  filter_status:
    name: "Filter Status"
  bypass_valve_open:
    name: "Bypass Valve Open"
```

#### Base options

| Option | Type | Description |
| --- | --- | --- |
| `id` | ID | Manually specify the ID of the component, needed for lambda calls. |
| `name` | string | Name of the generated `climate` entity. |
| `uart_id` | ID | ID of the UART bus the unit is connected to. |

Plus all options of the [base climate component](https://esphome.io/components/climate/index.html)
(`icon`, `entity_category`, `visual`, …).

> Requires a recent ESPHome version: the component uses the current climate traits
> feature-flag API (`add_feature_flags`) and the current entity registration
> (`climate.register_climate`).

> **Breaking change (Umstellung auf die aktuelle Climate-API):** früher wurde das `name:`
> aus der Config an einer eigenen `set_name()`-Methode vorbei gesetzt, die Entity selbst blieb
> namenlos und erbte damit den Gerätenamen (`climate.comfoair`). Jetzt wird `name:` als
> normaler Entity-Name verwendet, Home Assistant bildet die entity_id also aus Gerätename +
> Entity-Name (z. B. `climate.central_comfoair_350`). Die alte Entity bleibt als
> "nicht verfügbar" in der HA-Entity-Registry zurück: dort löschen und die neue Entity bei
> Bedarf wieder auf die alte entity_id umbenennen (Historie wandert mit).

### Climate entity

The component registers a climate device with:

* Modes: `off` (level 1 / away), `fan_only` (levels 2…4), `auto` (level 0)
* Fan modes: `off`, `low`, `medium`, `high`, `auto` → ventilation levels 1…4 and auto

Mode and fan mode are two views of the same ventilation level, so changing one also updates
the other: selecting `auto` sets fan mode `auto`, `off` sets fan mode `off`, and `fan_only`
keeps the current level (or falls back to `low` if the unit is off or in auto).
* Target temperature (comfort temperature): 12 … 29 °C, 1 °C steps
* Current temperature from the unit

Nach einem Neustart meldet die Entity kurz `off`, bis die erste Antwort der Lüftung
eingetroffen ist (der Level wird direkt in `setup()` angefragt, also typischerweise
< 1 s). Bleibt der Modus dauerhaft auf `off`, obwohl die Anlage läuft, steht sie auf
Stufe 1 (Abwesenheit) – diese Stufe wird bewusst auf `off` gemappt.

### Optional sensors

All entries below are optional and take a normal ESPHome sensor configuration
(`name`, `id`, `filters`, …).

**Sensors**

`intake_fan_speed`, `exhaust_fan_speed`, `intake_fan_speed_rpm`, `exhaust_fan_speed_rpm`,
`ventilation_level`, `outside_air_temperature`, `supply_air_temperature`,
`return_air_temperature`, `exhaust_air_temperature`, `enthalpy_temperature`,
`ewt_temperature`, `reheating_temperature`, `kitchen_hood_temperature`,
`return_air_level`, `supply_air_level`, `bypass_valve`, `bypass_factor`, `bypass_step`,
`bypass_correction`, `bypass_open_hours`, `motor_current_bypass`,
`motor_current_preheating`, `preheating_hours`, `level0_hours`, `level1_hours`,
`level2_hours`, `level3_hours`, `frost_protection_hours`, `frost_protection_minutes`,
`filter_hours`, `bathroom_switch_on_delay_minutes`, `bathroom_switch_off_delay_minutes`,
`l1_switch_off_delay_minutes`, `boost_ventilation_minutes`, `filter_warning_weeks`,
`rf_high_time_short_minutes`, `rf_high_time_long_minutes`,
`extractor_hood_switch_off_delay_minutes`

**Binary sensors**

`bypass_present`, `enthalpy_present`, `ewt_present`, `options_present`,
`fireplace_present`, `kitchen_hood_present`, `postheating_present`,
`postheating_pwm_mode_present`, `preheating_present`, `bypass_valve_open`,
`preheating_state`, `summer_mode`, `supply_fan_active`, `frost_protection_active`,
`p10_active` … `p19_active`, `p90_active` … `p97_active`

**Text sensors**

`type`, `size`, `filter_status`, `frost_protection_level`, `preheating_valve`

### Actions from lambdas

```yaml
button:
  - platform: template
    name: "Reset Filter"
    on_press:
      - lambda: id(comfoair1).filter_reset();

  - platform: template
    name: "Reset Errors"
    on_press:
      - lambda: id(comfoair1).error_reset();
```

| Method | Description |
| --- | --- |
| `filter_reset()` | Reset the filter change counter. |
| `error_reset()` | Reset the error/fault state and run the self test. |
| `set_fans_intake_only()` | Run the intake fan only (supply). |
| `set_fans_exhaust_only()` | Run the exhaust fan only. |
| `set_fans_intake_and_exhaust()` | Restore normal operation of both fans. |

> The fan level commands write the ventilation level table of the unit. Use them with care
> and only if you understand the consequences for your ventilation system.

---

## current_tilt_based

A time based `cover` platform for roller shutters and venetian blinds that are driven by two
relays (open/close) and monitored with a **motor current sensor per direction**.

Instead of relying on timing alone, the component watches the motor current:

* **End position detection** – when the measured current drops below the `*_moving_current_threshold`
  (after `start_sensing_delay`), the shutter has reached its end position and the position is set to
  fully open / fully closed. This keeps the position in sync even after manual operation or drift.
* **Obstacle detection** – when the current rises above the optional `*_obstacle_current_threshold`,
  the movement is stopped and the cover rolls back by `obstacle_rollback`.
* **Malfunction detection** – if current is measured on the *opposite* direction sensor while moving
  (e.g. both relays energized, wiring fault), the cover is stopped and `malfunction_action` is executed.
* **Interlock / dead time** – `interlock_wait_time` and `dir_change_dead_time` prevent both directions
  from being energized at (nearly) the same time when a direction change is requested.
* **Tilt** – if `tilt_duration` is set, tilt is supported. Full open/close tilt commands from the
  Home Assistant UI are translated into steps of `tilt_step_size` instead of a full tilt sweep.

### Configuration

```yaml
cover:
  - platform: current_tilt_based
    id: living_room_blind
    name: "Living Room Blind"

    open_action:
      - switch.turn_off: relay_close
      - switch.turn_on: relay_open
    open_duration: 25s
    open_sensor: current_open
    open_moving_current_threshold: 0.10
    open_obstacle_current_threshold: 1.20

    close_action:
      - switch.turn_off: relay_open
      - switch.turn_on: relay_close
    close_duration: 24s
    close_sensor: current_close
    close_moving_current_threshold: 0.10
    close_obstacle_current_threshold: 1.20

    stop_action:
      - switch.turn_off: relay_open
      - switch.turn_off: relay_close

    tilt_duration: 1.5s
    tilt_step_size: 10%
    interlock_wait_time: 500ms
    dir_change_dead_time: 300ms
    obstacle_rollback: 10%
    max_duration: 60s
    malfunction_detection: true
    malfunction_action:
      - switch.turn_off: relay_open
      - switch.turn_off: relay_close
      - logger.log: "Blind malfunction detected"
```

### Options

| Option | Type | Default | Description |
| --- | --- | --- | --- |
| `open_action` | automation | **required** | Actions to start opening (energize the open relay). |
| `open_duration` | time | **required** | Time for a full open travel. |
| `open_sensor` | ID | **required** | Sensor providing the motor current while opening. |
| `open_moving_current_threshold` | float | **required** | Current above which the motor is considered to be running (A). |
| `open_obstacle_current_threshold` | float | – | Current above which an obstacle is assumed while opening (A). |
| `close_action` | automation | **required** | Actions to start closing. |
| `close_duration` | time | **required** | Time for a full close travel. |
| `close_sensor` | ID | **required** | Sensor providing the motor current while closing. |
| `close_moving_current_threshold` | float | **required** | Current above which the motor is considered to be running (A). |
| `close_obstacle_current_threshold` | float | – | Current above which an obstacle is assumed while closing (A). |
| `stop_action` | automation | **required** | Actions to stop the motor (de-energize both relays). |
| `tilt_duration` | time | `0ms` | Time for a full tilt movement. `0ms` disables tilt support. |
| `tilt_step_size` | percentage | `10%` | Step size used for tilt open/close commands. |
| `interlock_wait_time` | time | `500ms` | Minimum pause before starting the opposite direction. |
| `dir_change_dead_time` | time | `0ms` | Dead time after a direction change during which new commands are ignored. |
| `obstacle_rollback` | percentage | `10%` | Distance the cover moves back after an obstacle was detected. `0%` disables rollback. |
| `max_duration` | time | – | Hard limit for a single movement; the cover is stopped when reached. |
| `malfunction_detection` | boolean | `true` | Enable detection of current on the opposite direction sensor. |
| `malfunction_action` | automation | – | Actions executed when a malfunction is detected. |
| `start_sensing_delay` | time | `500ms` | Delay after start before the current is evaluated (motor inrush / startup). |

Plus all options of the [base cover component](https://esphome.io/components/cover/index.html)
(`name`, `device_class`, `id`, …).

### Notes

* Both current sensors should be updated fast (e.g. an ADC or a current sensor with a short
  `update_interval`), otherwise end positions and obstacles are detected late.
* Set the `*_moving_current_threshold` above the noise floor of the idle motor, and the
  `*_obstacle_current_threshold` clearly above the normal running current.
* A direction change while the cover is moving only stops the cover; send the command again
  to start the new direction.

---

## Disclaimer

These components are provided as-is, without any warranty. Driving shutter motors and
modifying the settings of a ventilation unit can damage your hardware — verify your wiring
and thresholds before using them in production.
