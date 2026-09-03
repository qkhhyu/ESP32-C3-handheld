# Codex usage bridge

The ESP32 must not store a Codex OAuth token. Run the bridge on the PC that is
logged in to Codex; it calls the current local account and exposes only quota
percentages and reset countdowns to the LAN.

## Start it

1. Ensure `codex login` has completed on the PC.
2. Choose a long random LAN token (at least 16 characters).
3. In the repository root, run:

   ```powershell
   python tools/codex_usage_bridge.py --token 'replace-with-a-long-random-token'
   ```

4. Allow inbound TCP port `8787` on the private Windows firewall profile if it
   is blocked. Find the PC's LAN IPv4 address with `ipconfig`.
5. Copy `main/usage_secrets.example.h` to `main/usage_secrets.h`, then set:

   ```c
   #define CODEX_BRIDGE_URL "http://192.168.1.10:8787/usage"
   #define CODEX_BRIDGE_TOKEN "replace-with-a-long-random-token"
   ```

`usage_secrets.h` remains ignored by Git. The bridge token protects only the
LAN endpoint; it is not an OpenAI credential. Keep the PC and ESP32 on a
trusted LAN.

## Endpoint contract

The device sends `GET /usage` with `X-Usage-Token`. A successful response is
small and needs no decompression:

```json
{"primary":{"used_percent":12,"reset_after_seconds":2847},"secondary":{"used_percent":4,"reset_after_seconds":200000},"plan":"plus"}
```

The firmware shows `primary` as Codex `5H` and `secondary` as `WK`, consistent
with the usual Codex windows. It keeps the last valid snapshot if the bridge,
PC, or upstream service is unavailable. The bridge caches upstream results for
30 seconds and never sends the contents of `auth.json` to the ESP32.

The upstream ChatGPT endpoint is an account-backed surface and can evolve. If
it changes, update only `tools/codex_usage_bridge.py`; the ESP32 contract stays
the compact JSON above.
