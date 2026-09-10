# Customer Email Template (English)

Subject: Reference Patch Set for Quectel BG95/EG91xQ on Zephyr

Hi <Customer Name>,

We have prepared a reference patch set based on Zephyr main and validated it in repeated PPP-over-UART lifecycle tests.

Reference branch:

- customer/bg95-cmux-cfun-reset

Fork repository:

- https://github.com/pioneerAlone/zephyr

What is included:

1. BG95/BG96 (BG9x) CMUX configuration update using explicit full parameters:
   - AT+CMUX=0,0,5,127,10,3,30,10,2
2. BG9x shutdown-hook adjustment to software reset fallback:
   - AT+CFUN=1,1
   - Note: this was selected for our STM32 validation setup because host GPIO voltage domain did not match modem PWRKEY domain.
3. EG91xQ driver integration and binding support.
4. Shared modem_cellular state-machine refinements for lifecycle robustness.

Validation coverage status:

- So far, MQTT validation has been completed on non-TLS transport (plain MQTT).
- MQTT over TLS validation is planned next, and we will share results in a follow-up.

Important note:

- The AT+CFUN=1,1 fallback is a validation-platform constraint, not a universal requirement.
- If your board has a reliable and electrically compatible PWRKEY path, you may prefer the original QPOWD-based flow.

Recommended adoption path:

1. Review the branch diff.
2. Port/cherry-pick the relevant files into your sdk-zephyr branch.
3. Validate CMUX bring-up, PPP lifecycle stability, and suspend/recovery behavior on your hardware.

Documentation:

- See the reference note in our package: Docs/BG95_Customer_Reference.md

If needed, we can also provide a minimal application-side integration example (overlay + prj.conf + startup flow) for faster reproduction.

Best regards,
<Your Name>
