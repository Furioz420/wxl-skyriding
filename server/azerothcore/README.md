# mod-skyriding

AzerothCore reference implementation for the WarcraftXL `wxl-skyriding` client module.

This directory is optional external setup. WXL Hub does not install, merge, configure, or update a
server core. Review these files against the exact AzerothCore revision in use before integrating
them. Other server cores may implement the same opcode, movement, and spell-charge contracts.

## Requirements

- AzerothCore with module support enabled.
- Server and client spell records for the retail IDs listed below.
- WarcraftXL v1.1 with `wxl-skyriding` and its declared client dependencies.
- Compatible Spell Charge core support.
- A mount model containing the AdvFly sequences expected by the WXL module.

The `SkyridingBar` addon is optional development UI. Set
`Skyriding.DevAddonMessages = 1` to mirror server telemetry to it; production
movement and control use `CMSG/SMSG_WXL_SKYRIDING` and do not require addon chat.

The module is standalone by default. Aura `98052` from the upstream riding
overhaul is only required when `Skyriding.RequireFlightChargesAura = 1`.

The default display-ID whitelist targets RidingWyvern variants used by the reference data set.
Change `Skyriding.AllowedMountDisplayIds` when adding another AdvFly-capable mount.

## Retail spell contract

- `361584` Whirling Surge
- `372608` Surge Forward
- `372610` Skyward Ascent
- `372773` Vigor
- `376777` Skyriding Basics
- `383359` Skyriding Charges
- `383363` Lift Off
- `383366` Thrill of the Skies
- `403092` Aerial Halt
- `404464` Flight Style: Skyriding
- `404468` Flight Style: Steady
- `404471` Change Flight Style
- `425782` Second Wind

The two flight-style passives are mutually exclusive runtime auras. The
selected style is restored on login, and Vigor is applied while a player is
using a fly-capable Skyriding mount. Change Flight Style swaps the style while
grounded and dismounts the player. Steady style bypasses the module completely,
leaving the mount's normal WotLK flight behavior intact.

`372610` is the normal action-bar Skyward Ascent spell while airborne and
consumes one shared vigor charge. Ground takeoff is deliberately separate:
double-Space sends the server-authoritative `TAKEOFF` request and performs the
same ascent for free. With `Skyriding.AutoLearnAbilities = 1`, the server
learns `372610` on login; its client `Spell.dbc` row must remain active,
non-hidden, and carry WotLK `SPELL_ATTR0_ALLOW_WHILE_MOUNTED` (`0x01000000`).
