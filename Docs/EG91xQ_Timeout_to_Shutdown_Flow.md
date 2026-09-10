# EG91xQ Timeout -> Shutdown Flow (Zephyr modem_cellular)

## Symptom Observed

- `quectel_eg91xq_init_chat_script: timed out`
- or `quectel_eg91xq_network_chat_script: timed out`
- then app prints `PPP phase not running in time: -116`
- then app calls suspend and modem runs shutdown script:
  - `quectel_eg91xq_shutdown_chat_script: timed out`

## Why shutdown runs

Shutdown is not triggered directly by init/network timeout alone in this test path.
It is triggered by app-side suspend after PPP wait timeout.

App path:

1. `ppp_session_wait_connected(..., 120s)` timeout.
2. `app_startup_run()` goes to fail path.
3. fail path calls:
   - `ppp_session_bring_down(iface)`
   - `modem_device_suspend(modem)`
4. `modem_device_suspend()` calls `pm_device_action_run(..., PM_DEVICE_ACTION_SUSPEND)`.
5. modem_cellular receives `MODEM_CELLULAR_EVENT_SUSPEND` and enters power-off sequence.

## Power-off sequence in modem_cellular

1. Enter `MODEM_CELLULAR_STATE_INIT_POWER_OFF`.
2. Try to disconnect CMUX and wait for command pipe availability.
3. If vendor shutdown script exists and command pipe is available:
   - enter `MODEM_CELLULAR_STATE_RUN_SHUTDOWN_SCRIPT`
   - run vendor shutdown script (`AT+CEREG=0`, `AT+QPOWD=1` for current EG91xQ driver)
4. If shutdown script fails/times out:
   - fallback to power-off pulse if power GPIO exists
   - otherwise go directly to IDLE.

## Separate path: script failure retry and self-suspend

For init/network/dial/apn scripts, modem_cellular increments `script_failure_counter`.
When counter reaches `CONFIG_MODEM_CELLULAR_MAX_SCRIPT_FAILURES` (default 3),
modem_cellular internally emits SUSPEND and enters the same power-off sequence.

So there are two ways to reach shutdown:

- App-driven suspend (your current logs show this path clearly)
- Driver self-suspend after repeated script failures

## Important EG91xQ script differences to review

Current initial EG91xQ script choices may be too strict:

- `AT+CMUX=0,0,5,127` currently expects `OK` response.
- Many modem drivers use RESP_NONE for CMUX entry because the modem can switch mode immediately and not return a normal `OK` line.

Also check network script command compatibility on EG91xQ firmware:

- `AT+QCFG="cmux/urcport",1`

If command support/behavior differs by firmware, it can stall or fail script progress.

## Practical next debug step

1. Re-enable debug logs temporarily:
   - modem state transition logs
   - modem chat raw RX logs
2. Verify exact command where timeout occurs in EG91xQ script.
3. Tune only that step (for example CMUX response mode) and retest.
