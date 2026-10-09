# Incoming SMS retention boundary

Incoming SMS text, PDU responses, raw receive buffers and channel-variable values are omitted from plugin logs. Delivery metadata remains available. This deliberately retires consumers that reconstruct incoming bodies from verbose logs; use the SMS JSON dialplan variable through a body-safe ingress such as the paired Telegram Bot AGI.

Multipart assembly uses a TEMP table with `temp_store=MEMORY`, even when the outgoing delivery database is file-backed. It is excluded from `VACUUM INTO` backups. Expired parts are removed on subsequent multipart ingress, and admission is bounded to 1,024 parts and 1 MiB of UTF-8 content. Completed assemblies are deleted. Capacity or assembly failures acknowledge/discard the part and never forward an incomplete fragment as an ordinary SMS.

Outgoing tables, acknowledgements and delivery reporting retain their existing behavior. Historical disk tables and backups are preserved; this change does not erase previously retained bodies. The existing connection-wide transaction serialization issue outside multipart ingress is not addressed here.

This boundary requires body-free dialplan arguments, core variable debug disabled, AGI debug off, and no AMI/Stasis consumer that persists SMS variables. Asterisk core itself can log variable contents under diagnostic settings. Modem/carrier retention and forensic RAM erasure are outside this boundary.
