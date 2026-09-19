# WeatherClock ESP-AT BLE

The ESP32-C3 runs ESP-AT as a BLE GATT server. Before running the STM32
application, replace ESP-AT's
`components/customized_partitions/raw_data/ble_data/gatts_data.csv` with the
file in this directory, generate `mfg_nvs.bin`, and flash that partition to
the ESP32-C3. Flashing only the stock ESP-AT firmware is not enough because
the stock GATT table does not contain the UUIDs below.

The table defines one service:

- `0xFFF0`: Weather service
- `0xFFF1`: Environment value, Read + Notify, 4-byte little-endian value
  (`int16 temperature * 100`, `uint16 humidity * 100`)
- `0xFFF2`: Sample period, Read + Write, one or two ASCII digits (`1` to `60`)

The STM32 code expects server service index 1, environment characteristic
index 1 and sample-period characteristic index 2. Verify these indices with
`AT+BLEGATTSSRV?` and `AT+BLEGATTSCHAR?` after flashing the table.

At startup the STM32 configures ESP-AT as a GATT server, enables connection
status messages, advertises the name `WeatherClock` and includes service UUID
`0xFFF0` in the advertising data.

Use nRF Connect for Mobile to connect to `WeatherClock`, subscribe to `0xFFF1`,
and write an ASCII/UTF-8 value such as `5` to `0xFFF2`. A notification contains
four bytes in little-endian order. For example, temperature `25.34 C` and
humidity `61.20%` are encoded as `E6 09 E8 17`.
