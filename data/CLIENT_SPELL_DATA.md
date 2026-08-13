# Client and server spell-data contract

Skyriding uses retail spell IDs, but the 3.3.5a client still requires rows laid out and attributed as
WotLK `Spell.dbc` records. Do not copy retail `SpellMisc` attribute masks into the WotLK table.

| Spell ID | Name | Client behavior |
|---:|---|---|
| 361584 | Whirling Surge | Active self dummy, mounted, 30-second cooldown |
| 372608 | Surge Forward | Active self dummy using the shared vigor pool |
| 372610 | Skyward Ascent | Active self dummy using the shared vigor pool |
| 372773 | Vigor | Passive runtime dummy aura |
| 376777 | Skyriding Basics | Passive dummy aura |
| 383359 | Skyriding Charges | Passive dummy aura |
| 383363 | Lift Off | Passive dummy aura |
| 383366 | Thrill of the Skies | Passive dummy aura |
| 403092 | Aerial Halt | Active self dummy, mounted, 10-second cooldown |
| 404464 | Flight Style: Skyriding | Passive style marker |
| 404468 | Flight Style: Steady | Passive style marker |
| 404471 | Change Flight Style | Active self dummy, 1.5-second cast |
| 425782 | Second Wind | Active self dummy, mounted, one-second client lockout |

Passive rows need the WotLK passive attribute, a dummy aura targeting self, and infinite duration.
Active mounted spells need the WotLK `SPELL_ATTR0_ALLOW_WHILE_MOUNTED` bit. The server is authoritative
for charge counts and recharge timing; WotLK `Spell.dbc` cannot represent the retail charge model.

The AzerothCore module should ship its own `spell_dbc` SQL base rows and forward updates. The client
data pack should merge only the matching WotLK-compatible DBC rows and required icons. Retail icon
FileDataIDs are not WotLK `SpellIconID` values and must be converted or mapped separately.
