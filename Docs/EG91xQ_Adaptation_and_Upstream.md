# EG91xQ Adaptation Plan and Zephyr Upstream Guide

## 1. What Was Added Locally

To adapt from BG95 to EG91xQ with Zephyr modem_cellular + CMUX + PPP, the following local changes were made:

- Added a new devicetree binding: `quectel,eg91xq`
- Added a new vendor modem driver implementation for modem_cellular
- Wired the new driver into modem vendor CMake build
- Added EG91xQ to modem cellular Kconfig DT feature list
- Switched app board modem compatible from BG95 to EG91xQ

Changed files:

- `zephyr/dts/bindings/modem/quectel,eg91xq.yaml`
- `zephyr/drivers/modem/vendor_modem_cellular/cellular_quectel_eg91xq.c`
- `zephyr/drivers/modem/vendor_modem_cellular/CMakeLists.txt`
- `zephyr/drivers/modem/Kconfig.cellular`
- `armfly_network_demo/boards/armfly_stm32_v6/armfly_stm32_v6.dts`

## 2. Command Flow Chosen for EG91xQ

The EG91xQ script is based on your extracted docs and existing Zephyr Quectel patterns:

- Init:
  - `AT`
  - `ATE0`
  - `AT+CFUN=4`
  - `AT+CMEE=1`
  - `AT+CEREG=1`
  - `AT+CEREG?`
  - `AT+CGSN`
  - `AT+CGMM`
  - `AT+CGMI`
  - `AT+CGMR`
  - `AT+CIMI`
  - `AT+QCCID`
  - `AT+CMUX=0,0,5,127`
- Network setup:
  - `AT+QCFG="cmux/urcport",1`
  - `AT+CEREG=1`
  - `AT+CEREG?`
  - `AT+CFUN=1`
- Dial:
  - `AT+CGACT=0,1`
  - `AT+CFUN=1`
  - `AT` (short delay point)
  - `ATD*99***1#`
- Periodic:
  - `AT+CEREG?`
  - `AT+CSQ`
- Shutdown:
  - `AT+CEREG=0`
  - `AT+QPOWD=1`

## 3. Validation Checklist in Zephyr Workbench

Use your existing Workbench flow (the plain shell west environment may differ):

1. Clean build once after modem compatible change.
2. Flash and capture boot log.
3. Confirm these milestones in logs:
   - set baud/init scripts pass
   - CMUX connected
   - DLCI1/DLCI2 opened
   - registration event
   - dial success (`CONNECT ...`)
   - PPP phase running
   - PPP IPv4 acquired
   - ping success
4. Soak test for at least 30-60 minutes:
   - periodic script stable
   - no repeated recovery loops
   - no PPP flaps

## 4. If EG91xQ Fails at Specific Stage

Use stage-specific adjustments (prefer one change per test run):

- Fails before/at CMUX:
  - test `AT+CMUX=0` instead of full parameter form
  - increase init script timeout window
- Registers slowly:
  - increase await-registered timeout
  - keep periodic CEREG polling enabled
- Dials but no PPP traffic:
  - verify APN and CID=1 mapping (`AT+CGDCONT=1,"IP","<apn>"`)
  - keep `AT` guard command before `ATD*99***1#`

## 5. How to Contribute Upstream to Zephyr

### 5.1 Prepare Branch

In your zephyr repository clone:

- `git checkout main`
- `git pull --rebase`
- `git checkout -b modem-eg91xq-support`

### 5.2 Split Commits Clearly

Recommended commit split:

1. DT binding commit
   - add `dts/bindings/modem/quectel,eg91xq.yaml`
2. Driver commit
   - add `drivers/modem/vendor_modem_cellular/cellular_quectel_eg91xq.c`
   - update `drivers/modem/vendor_modem_cellular/CMakeLists.txt`
   - update `drivers/modem/Kconfig.cellular`
3. (Optional) sample/board usage commit in your own repo (not mandatory for Zephyr core)

Commit style suggestion:

- `dts: bindings: modem: add quectel,eg91xq`
- `drivers: modem: cellular: add quectel eg91xq support`

### 5.3 Verify Before PR

Run checks you can execute locally (at minimum build + runtime log evidence):

- Build for your target board with EG91xQ node enabled
- Capture runtime logs showing CMUX + PPP success
- Ensure no unrelated file formatting churn

### 5.4 Open PR

1. Push branch to your fork.
2. Open PR against `zephyrproject-rtos/zephyr`.
3. In PR description include:
   - module tested (exact EG91xQ variant)
   - UART wiring mode (UART-only or with PWRKEY/RESET)
   - firmware version
   - key success logs (register, dial, PPP IPv4)
   - note that command sequence is based on Quectel EG91xQ docs and on-device validation

### 5.5 Responding to Review

Typical maintainer feedback areas:

- whether EG91xQ can reuse EG800Q driver vs dedicated file
- command script minimality
- timeout values and power sequencing defaults
- binding property requirements (`mdm-power-gpios` optional vs required)

Be ready to:

- trim commands not strictly needed
- document why each non-generic command exists
- provide additional logs for corner cases (reboot, SIM absent, weak signal)

## 6. Practical Recommendation

For now, keep your app using `quectel,eg91xq` and validate stability first.
After stability is confirmed, upstream the Zephyr-side changes only. Keep board/app changes in your project repo unless maintainers ask for a Zephyr sample update.
