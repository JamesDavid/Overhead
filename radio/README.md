# radio/ — DEFERRED (spec §10.3)

Placeholder only. The radio/Doppler node consumes the reserved radio fields of
`shared/telemetry.h` (`base_freq_hz`, `doppler_hz`, `range_rate_kms`) to steer a BTECH
UV-PRO over BLE — likely a Pi running benlink rather than an ESP32 reimplementing the GATT
service.

**Do not implement in the rotor task.** The telemetry contract already reserves the fields so
the packet shape never changes when this node arrives.
