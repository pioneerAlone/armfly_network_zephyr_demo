# Quectel Modem Driver Reference for Customer (nRF sdk-zephyr)

## 1. Purpose

This note summarizes the recently validated modem-driver updates prepared for customer reference.
Current scope includes:
- BG95/BG96 (BG9x driver path)
- EG91xQ vendor driver integration
- shared modem_cellular lifecycle/state-machine fixes

## 2. Baseline Information

- Validation codebase: Zephyr official main branch
- Validation snapshot: commit ceb28342bef
- Zephyr version line in validation tree: 4.4.99
- Customer target codebase: nrfconnect/sdk-zephyr
- Customer-facing fork repo: pioneerAlone/zephyr

## 3. Why Use AT+CFUN=1,1 in Shutdown Hook

On the STM32 validation board, the host GPIO voltage domain does not match the modem PWRKEY domain.
Without proper level-shifting hardware, hardware power-key reset is not consistently reliable.

To keep PPP over UART lifecycle tests reproducible, the BG9x shutdown hook was switched to software reset fallback:
- active path: AT+CFUN=1,1
- original AT+QPOWD=1 flow is preserved in source comments for A/B switching

Important:
This is a board-level electrical constraint from the validation platform, not a universal requirement for all customers.
If customer hardware provides a reliable PWRKEY path, they can switch back to QPOWD-based shutdown.

## 4. CMUX Parameter Decision for BG95

The init script uses explicit full CMUX parameters:
- AT+CMUX=0,0,5,127,10,3,30,10,2

Reason:
- aligns with BG95/BG96 MUX app-note default values
- avoids ambiguous behavior when SDK defaults differ

## 5. Code to Provide to Customer

Mandatory Zephyr files:
- drivers/modem/vendor_modem_cellular/cellular_quectel_bg9x.c
- drivers/modem/vendor_modem_cellular/cellular_quectel_eg91xq.c
- dts/bindings/modem/quectel,eg91xq.yaml
- drivers/modem/vendor_modem_cellular/CMakeLists.txt
- drivers/modem/Kconfig.cellular
- drivers/modem/modem_cellular.c

Reference files:
- this note
- project-level validation notes under Docs/

## 6. Recommended Integration Path

Option A (recommended):
1. Share branch and PR from your fork.
2. Customer reviews exact diff and ports to sdk-zephyr branch.

Option B:
1. Share commit hash.
2. Customer cherry-picks or manually applies the same file-level change.

## 7. What Customer Should Validate

1. Modem node compatible is set to quectel,bg95 or quectel,eg91xq in board DTS/overlay.
2. CMUX establishes cleanly after init.
3. PPP up/down lifecycle remains stable across multiple loops.
4. Shutdown path triggers expected modem reset behavior.

## 8. Current Validation Scope and Next Step

Current status:
- MQTT validation completed with non-TLS transport (plain MQTT).

Planned next step:
- MQTT over TLS validation is in progress and results will be shared in a follow-up update.

## 9. Should the Full Project Source Be Shared?

Short answer: usually not required at first.

Recommended sharing strategy:
1. Share Zephyr fork branch/PR first (driver-level reference).
2. Share a minimal application reference only if customer needs reproducible integration behavior.
3. Share full project source only when customer asks for end-to-end reproduction.

If sharing application source, include:
- exact overlay used
- prj.conf deltas related to modem/PPP/CMUX
- concise startup flow notes (resume, PPP up, AT check, MQTT, teardown)

Before sharing project source, remove or review:
- private broker hostnames, credentials, certificates, tokens
- internal-only scripts and board-specific paths
- non-customer debug artifacts

## 10. Branch and Commit Prepared

- Branch: customer/bg95-cmux-cfun-reset
- Latest commit: 80a93853b0a

If push authentication is pending in your terminal session, run:

git push -u origin customer/bg95-cmux-cfun-reset
