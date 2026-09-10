# BG95 <-> STM32 Modem Communication Call Chain

## Scope
This note documents the end-to-end call chain from application layer to Zephyr modem driver layer and PPP/ICMP ping data plane for this project.

Project assumptions:
- Zephyr main branch (4.4.99 development state)
- Board: armfly_stm32_v6
- Modem: Quectel BG95
- UART-only wiring (no PWRKEY control line)
- BG95 UART baudrate: 115200

## Current DTS Key Points
In `boards/armfly_stm32_v6/armfly_stm32_v6.dts` the modem node uses:
- `compatible = "quectel,bg95"`
- `autostarts;`
- no `mdm-power-gpios`

This means `modem_cellular` should not rely on external PWRKEY pulse control and should follow autostart logic.

## Layered Call Chain (Control Plane)

```text
[Business Layer]
main()
  file: src/main.c
        |
        v
app_startup_run()
  file: src/app_startup.c
  steps:
    - modem_device_resume()
    - ppp_session_bring_up()
    - ppp_session_wait_connected()
    - ppp_session_wait_ipv4_ready()
    - ppp_session_ping_ipv4()
        |
        v
[Device/PM Bridge]
modem_device_resume()
  file: src/modem_device.c
  call: pm_device_action_run(... RESUME)
        |
        v
[Zephyr Modem Core FSM]
modem_cellular_pm_action(RESUME)
  file: zephyr/drivers/modem/modem_cellular.c
        |
        v
idle_event_handler -> (autostarts path)
  states:
    await power on
      -> set baudrate script
      -> run init script
      -> connect cmux
      -> open dlci1
      -> open dlci2
      -> network/dial
      -> PPP running
        |
        v
[Vendor Script Layer: BG95]
quectel_bg9x_set_baudrate_chat_script
quectel_bg9x_init_chat_script
  file: zephyr/drivers/modem/vendor_modem_cellular/cellular_quectel_bg9x.c
        |
        v
[PPP Adapter Layer]
MODEM_DT_INST_PPP_DEFINE(...) creates PPP net device
  files:
    - zephyr/drivers/modem/vendor_modem_cellular/cellular_quectel_bg9x.c
    - zephyr/include/zephyr/modem/ppp.h
        |
        v
modem_ppp_ppp_api_send()
  file: zephyr/subsys/modem/modem_ppp.c
        |
        v
CMUX/DLCI + UART backend
  files:
    - zephyr/drivers/modem/modem_cellular.c
    - zephyr/drivers/modem/modem_iface_uart_*.c
        |
        v
[Physical Layer]
STM32 USART6 TX/RX <-> BG95 UART
```

## Ping Data Plane

```text
[App ping call]
ppp_session_ping_ipv4()
  file: src/ppp_session.c
  calls:
    - net_icmp_init_ctx(AF_INET, ECHO_REPLY, code=0, handler)
    - net_icmp_send_echo_request(...)
        |
        v
[Zephyr net stack]
route packet to PPP net_if
        |
        v
[PPP transmit]
modem_ppp_ppp_api_send()
  file: zephyr/subsys/modem/modem_ppp.c
        |
        v
modem pipe -> UART -> BG95 -> cellular network
        |
        v
ICMP Echo Reply returns via BG95 -> UART -> modem_ppp receive -> net_recv_data()
        |
        v
ppp_session_ping_reply_handler()
  file: src/ppp_session.c
  action: give semaphore -> ping function returns success
```

## What Was Already Fixed During Migration
- Zephyr main ICMP API compatibility in `src/ppp_session.c`:
  - updated `net_icmp_init_ctx()` call signature
  - updated ICMP callback return type to `enum net_verdict`
- Board file encoding issues (BOM) that broke Kconfig/OpenOCD parsing
- OpenOCD runner command incompatibility (`set_adapter_speed_if_not_set` removed)

## Practical Debug Focus (When Timeouts Reappear)
If modem scripts still timeout (`set baudrate` or `init script`), investigate in this order:
1. Physical UART path: TX/RX/GND wiring and voltage levels
2. Correct UART instance mapping: modem node currently under `&usart6`
3. BG95 actual running state and AT responsiveness at 115200
4. Console/log UART and modem UART line isolation (avoid crossing)
5. Keep autostarts path when no PWRKEY control line is connected

## Quick Checkpoints in Runtime Logs
Healthy progression should look like:
- `resume modem` (or already active)
- `await power on`
- `set baudrate` script success or benign skip
- `run init script` success
- `connect cmux / dlci open`
- PPP running and IPv4 ready
- ping send + ping success

If logs repeatedly loop into recovery with script timeouts, it typically indicates UART RX path does not deliver modem responses to the chat layer.
