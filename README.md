# wxl-skyriding

Native Skyriding movement and animation controller for WarcraftXL v1.1. The server remains
authoritative for state, vigor, charges, cooldowns, and impulses; this module owns client input,
movement presentation, and modern mount animation selection.

## Client dependencies

- `wxl-runtime` >= 1.0.0 for `wxl.network`.
- `wxl-modern-m2` >= 1.1.0 with the `wxl.m2-animation` v1 service.
- `wxl-spell-charges` >= 1.0.0 for the action-bar charge display used by Skyriding abilities.
- WarcraftXL v1.1 with the public opcode, movement, and extended-animation core contracts.

WXL Hub reads these dependencies from `wxl.json` and installs missing or incompatible client modules
in dependency-first order. Manual installation must follow the order shown above.

## Server and data requirements

The client DLL does not create a working Skyriding system on its own. It requires:

- a compatible server implementation of the Skyriding protocol;
- compatible server-side Spell Charge support;
- identical opcodes `0x527` through `0x52A` on client and server;
- WotLK-compatible client `Spell.dbc` rows and AzerothCore `spell_dbc` rows for the abilities listed
  in [data/CLIENT_SPELL_DATA.md](data/CLIENT_SPELL_DATA.md);
- a mount model carrying the required AdvFly animation sequences.

Retail DB2 rows are not copied directly into the WotLK DBC. `wxl-db2` may provide retail metadata and
visual lookup data, but the WotLK-compatible `Spell.dbc` rows remain a separate client data merge.
No private or full client database archive is distributed in this repository.

An optional AzerothCore reference implementation is included under
[`server/azerothcore`](server/azerothcore). It is not installed by WXL Hub and is not a requirement
for WarcraftXL development: another server core may implement the same protocol. Review and merge
server changes separately for the exact Core revision in use.

## Installation

Install with WXL Hub. The release ZIP contains `wxl-skyriding.dll` and `wxl-skyriding.cfg`; the Hub
places both under `Extensions\\wxl-skyriding`. Restart the client after installing or updating it.

For manual installation, extract the release ZIP to the same directory after installing every client
dependency. Configure and rebuild the server separately.
