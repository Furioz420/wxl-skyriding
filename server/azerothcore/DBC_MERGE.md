# Client Spell.dbc merge contract

Do not copy retail `SpellMisc` attribute masks directly into the 3.3.5
`Spell.dbc`; bit meanings and table layout differ. Use the working 372608 row
as the WotLK active-dummy template and specialize these fields.

| ID | Name | Kind | CastTimeIndex | RecoveryTime | Effect contract |
|---:|---|---|---:|---:|---|
| 361584 | Whirling Surge | active | 1 | 30000 | dummy, self, usable mounted |
| 372608 | Surge Forward | active | 1 | 0 | existing shared-charge dummy |
| 372610 | Skyward Ascent | active | 1 | 0 | existing shared-charge dummy |
| 372773 | Vigor | passive runtime aura | 1 | 0 | apply dummy aura to self |
| 376777 | Skyriding Basics | passive | 1 | 0 | apply dummy aura to self |
| 383359 | Skyriding Charges | passive | 1 | 0 | apply dummy aura to self |
| 383363 | Lift Off | passive | 1 | 0 | apply dummy aura to self |
| 383366 | Thrill of the Skies | passive | 1 | 0 | apply dummy aura to self |
| 403092 | Aerial Halt | active | 1 | 10000 | dummy, self, usable mounted |
| 404464 | Flight Style: Skyriding | passive marker | 1 | 0 | apply dummy aura to self |
| 404468 | Flight Style: Steady | passive marker | 1 | 0 | apply dummy aura to self |
| 404471 | Change Flight Style | active | 5 | 1500 | dummy, self; server requires ground |
| 425782 | Second Wind | active | 1 | 1000 | dummy, self, usable mounted |

For passive rows set the 3.3.5 passive attribute (`Attributes |= 0x40`),
`Effect_1 = 6`, `EffectAura_1 = 4`, `ImplicitTargetA_1 = 1`, and an infinite
duration. The server uses learned-spell state for persistence, so the dummy
aura itself is not authoritative.

Second Wind intentionally has only a one-second client lockout. Its three
charges, each recharging in 180 seconds, are implemented in the server module;
3.3.5 `Spell.dbc` cannot express retail spell charges. Surge Forward and
Skyward Ascent likewise use the module's shared six-charge pool.

Server compatibility records are installed into AzerothCore's `spell_dbc` by
`data/sql/db-world/base`, with forward migrations in `data/sql/db-world/updates`.
The WXL retail-spell overlay supplies the matching retail DB2 metadata without
replacing native WotLK spell rows.

Retail icon FileDataIDs observed in the PTR 12.1 exports include 237590
(Whirling Surge), 136243 (Surge/Skyward/Lift Off/Aerial Halt family), 236168
(Thrill of the Skies), 3306012 (Skyriding style), and 136222 (Change Flight
Style). These are not 3.3.5 `SpellIconID` values; map/import the corresponding
icons separately or retain temporary WotLK icon IDs.
