STM32 Modem Demo (PPP over UART + MQTT over TLS)
#################################################

Overview
********

This application demonstrates a customer-facing cellular data path on ARMFLY
STM32-V6:

1. PPP over UART (modem -> CMUX -> PPP)
2. MQTT over TLS on top of that PPP link

Current target flow:

1. Use a dedicated modem UART (``modem-uart`` alias).
2. Bring up modem through ``MODEM_CELLULAR`` driver.
3. Let Zephyr modem stack configure CMUX internally.
4. Start PPP over CMUX data channel and wait for connectivity events.
5. Resolve a public MQTT broker by DNS and run MQTT over TLS.

Hardware flow control is intentionally disabled for now.

Code Structure
**************

- ``src/main.c``: Entry point, long-running app loop.
- ``src/app_startup.c``: Startup orchestration for modem and PPP.
- ``src/modem_device.c``: Modem device lookup and resume handling.
- ``src/ppp_session.c``: PPP interface lookup, bring-up, and connection wait.
- ``src/mqtt_tls_client.c``: MQTT/TLS connect, subscribe, publish, and receive loop.
- ``src/at_control_service.c``: Application-side AT control on modem user pipe.

Devicetree Integration
**********************

Board devicetree defines:

- ``aliases { modem = &modem; modem-uart = &usart6; }``
- BG95 modem node under ``&usart6`` with compatible ``quectel,bg95``

Important:
Update ``mdm-power-gpios`` to your real board pin mapping.

Building
********

Use Zephyr Workbench in VS Code with board ``armfly_stm32_v6``.

Command line example:

.. code-block:: powershell

   west build -b armfly_stm32_v6 d:/workbench_proj/armfly_network_demo --build-dir d:/workbench_proj/armfly_network_demo/build/primary

Runtime Notes
*************

- ICMP ping to ``1.1.1.1`` is used for connectivity validation.
- If an operator blocks ICMP, PPP may still be up while ping check fails.

MQTT over TLS Demo Defaults
***************************

The current demo targets:

- Hostname: from ``CONFIG_APP_MQTT_HOSTNAME``
- Port: from ``CONFIG_APP_MQTT_PORT``
- Topic: from ``CONFIG_APP_MQTT_TOPIC``

These values are controlled by application Kconfig options.

Security note:

- ``CONFIG_APP_MQTT_USE_TLS=n`` selects plain MQTT, typically using port
   ``1883``.
- ``CONFIG_APP_MQTT_USE_TLS=y`` selects MQTT over TLS, typically using port
   ``8883``.
- ``CONFIG_APP_MQTT_TLS_VERIFY_PEER=y`` requires a valid broker CA
   certificate.
- ``CONFIG_APP_MQTT_TLS_MUTUAL_AUTH=y`` additionally requires a client
   certificate and private key.

Certificate Integration
***********************

Fill certificates in ``src/mqtt_tls_credentials_data.h``:

- ``app_mqtt_ca_certificate[]`` for one-way TLS
- ``app_mqtt_client_certificate[]`` for mutual TLS
- ``app_mqtt_private_key[]`` for mutual TLS

Runtime behavior:

- If peer verification is enabled but the CA certificate is empty, MQTT setup
   fails early.
- If mutual TLS is enabled but the client certificate or private key is empty,
   MQTT setup fails early.

Expected Successful Logs
************************

After modem and PPP are up, success path looks like:

- ``[app] PPP connectivity check passed``
- ``[mqtt] CONNACK ok``
- ``[mqtt] SUBACK message_id=...``
- ``[mqtt] TX topic='armfly/demo/ppp_tls' payload='...'``
- ``[mqtt] demo finished``
- ``[app] MQTT over TLS demo passed``

Quick Debug Checklist
*********************

If MQTT step fails while PPP step passes:

1. Check DNS reachability over PPP (broker hostname resolution).
2. Check operator policy for TCP/8883 outbound access.
3. If switching to strict certificate verification, ensure CA chain and time
    synchronization are both valid.

AT Interaction and Soft Reset
*****************************

This demo enables ``CONFIG_MODEM_AT_USER_PIPE`` and uses user pipe index 0
(DLCI3 for current EG91xQ integration) for app-controlled AT commands.

Current application behavior:

- Runs proactive AT health commands (``AT`` and ``AT+CEREG?``) before MQTT.
- If repeated startup failures reach
   ``CONFIG_APP_MODEM_SOFT_RESET_THRESHOLD``, the app tears PPP down first and
   then uses ``AT+CFUN=1,1`` as the final software reset escalation path.

Shutdown Policy for EG91xQ
**************************

The current EG91xQ vendor driver intentionally does not define a shutdown AT
script. In deployments where the host cannot assert modem power key signals, a
software power-down sequence can leave the modem off with no reliable way to
start it again. For such systems, the application keeps ``AT+CFUN=1,1`` as the
last-resort recovery action instead of using software shutdown as a normal
path.
