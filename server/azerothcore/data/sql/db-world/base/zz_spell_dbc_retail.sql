-- WotLK-compatible server records for the remaining retail Skyriding contract.
-- The client Spell.dbc must contain matching IDs. Runtime behavior is scripted
-- by mod-skyriding; these rows provide cast metadata, names and passive markers.

DELETE FROM `spell_dbc` WHERE `ID` IN
    (361584, 372773, 376777, 383359, 383363, 383366, 403092,
     404464, 404468, 404471, 425782);

DROP TEMPORARY TABLE IF EXISTS `_mod_skyriding_spell_template`;
CREATE TEMPORARY TABLE `_mod_skyriding_spell_template` LIKE `spell_dbc`;

-- Clone the already-defined Surge Forward dummy record, then specialize it.
INSERT INTO `_mod_skyriding_spell_template`
SELECT * FROM `spell_dbc` WHERE `ID` = 372608;
UPDATE `_mod_skyriding_spell_template` SET
    `ID` = 361584,
    `RecoveryTime` = 30000,
    `Name_Lang_enUS` = 'Whirling Surge',
    `NameSubtext_Lang_enUS` = 'Skyriding',
    `Description_Lang_enUS` = 'Spiral forward a great distance, increasing speed.';
INSERT INTO `spell_dbc` SELECT * FROM `_mod_skyriding_spell_template`;
DELETE FROM `_mod_skyriding_spell_template`;

INSERT INTO `_mod_skyriding_spell_template`
SELECT * FROM `spell_dbc` WHERE `ID` = 372608;
UPDATE `_mod_skyriding_spell_template` SET
    `ID` = 403092,
    `RecoveryTime` = 10000,
    `Name_Lang_enUS` = 'Aerial Halt',
    `NameSubtext_Lang_enUS` = 'Skyriding',
    `Description_Lang_enUS` = 'Flap back, reducing forward movement and briefly reducing gravity.';
INSERT INTO `spell_dbc` SELECT * FROM `_mod_skyriding_spell_template`;
DELETE FROM `_mod_skyriding_spell_template`;

-- Second Wind has a one-second client lockout. Its three 180-second charges are
-- authoritative in the module because 3.3.5 Spell.dbc has no spell-charge model.
INSERT INTO `_mod_skyriding_spell_template`
SELECT * FROM `spell_dbc` WHERE `ID` = 372608;
UPDATE `_mod_skyriding_spell_template` SET
    `ID` = 425782,
    `RecoveryTime` = 1000,
    `Name_Lang_enUS` = 'Second Wind',
    `NameSubtext_Lang_enUS` = 'Skyriding',
    `Description_Lang_enUS` = 'Instantly regenerates one shared Skyriding charge.';
INSERT INTO `spell_dbc` SELECT * FROM `_mod_skyriding_spell_template`;
DELETE FROM `_mod_skyriding_spell_template`;

-- Passive dummy-aura records. Learned-spell state is the persistence mechanism;
-- the scripted behavior uses Vigor and the flight-style auras as its runtime
-- state contract.
INSERT INTO `_mod_skyriding_spell_template`
SELECT * FROM `spell_dbc` WHERE `ID` = 372608;
UPDATE `_mod_skyriding_spell_template` SET
    `ID` = 372773,
    `Attributes` = `Attributes` | 64,
    `RecoveryTime` = 0,
    `Effect_1` = 6,
    `EffectAura_1` = 4,
    `ImplicitTargetA_1` = 1,
    `DurationIndex` = 21,
    `Name_Lang_enUS` = 'Vigor',
    `NameSubtext_Lang_enUS` = 'Skyriding',
    `Description_Lang_enUS` = 'Enables Vigor while using a Skyriding mount.';
INSERT INTO `spell_dbc` SELECT * FROM `_mod_skyriding_spell_template`;
DELETE FROM `_mod_skyriding_spell_template`;

INSERT INTO `_mod_skyriding_spell_template`
SELECT * FROM `spell_dbc` WHERE `ID` = 372608;
UPDATE `_mod_skyriding_spell_template` SET
    `ID` = 376777,
    `Attributes` = `Attributes` | 64,
    `RecoveryTime` = 0,
    `Effect_1` = 6,
    `EffectAura_1` = 4,
    `DurationIndex` = 21,
    `Name_Lang_enUS` = 'Skyriding Basics',
    `NameSubtext_Lang_enUS` = 'Skyriding',
    `Description_Lang_enUS` = 'Falling from a height opens the mount wings and begins skyriding.';
INSERT INTO `spell_dbc` SELECT * FROM `_mod_skyriding_spell_template`;
DELETE FROM `_mod_skyriding_spell_template`;

INSERT INTO `_mod_skyriding_spell_template`
SELECT * FROM `spell_dbc` WHERE `ID` = 372608;
UPDATE `_mod_skyriding_spell_template` SET
    `ID` = 383359,
    `Attributes` = `Attributes` | 64,
    `RecoveryTime` = 0,
    `Effect_1` = 6,
    `EffectAura_1` = 4,
    `DurationIndex` = 21,
    `Name_Lang_enUS` = 'Skyriding Charges',
    `NameSubtext_Lang_enUS` = 'Skyriding',
    `Description_Lang_enUS` = 'Skyriding abilities spend shared charges that recover over time.';
INSERT INTO `spell_dbc` SELECT * FROM `_mod_skyriding_spell_template`;
DELETE FROM `_mod_skyriding_spell_template`;

INSERT INTO `_mod_skyriding_spell_template`
SELECT * FROM `spell_dbc` WHERE `ID` = 372608;
UPDATE `_mod_skyriding_spell_template` SET
    `ID` = 383363,
    `Attributes` = `Attributes` | 64,
    `RecoveryTime` = 0,
    `Effect_1` = 6,
    `EffectAura_1` = 4,
    `DurationIndex` = 21,
    `Name_Lang_enUS` = 'Lift Off',
    `NameSubtext_Lang_enUS` = 'Skyriding',
    `Description_Lang_enUS` = 'Double jump while grounded to launch upward and begin skyriding.';
INSERT INTO `spell_dbc` SELECT * FROM `_mod_skyriding_spell_template`;
DELETE FROM `_mod_skyriding_spell_template`;

INSERT INTO `_mod_skyriding_spell_template`
SELECT * FROM `spell_dbc` WHERE `ID` = 372608;
UPDATE `_mod_skyriding_spell_template` SET
    `ID` = 383366,
    `Attributes` = `Attributes` | 64,
    `RecoveryTime` = 0,
    `Effect_1` = 6,
    `EffectAura_1` = 4,
    `DurationIndex` = 21,
    `Name_Lang_enUS` = 'Thrill of the Skies',
    `NameSubtext_Lang_enUS` = 'Skyriding',
    `Description_Lang_enUS` = 'At high speed, shared Skyriding charges recover faster.';
INSERT INTO `spell_dbc` SELECT * FROM `_mod_skyriding_spell_template`;
DELETE FROM `_mod_skyriding_spell_template`;

INSERT INTO `_mod_skyriding_spell_template`
SELECT * FROM `spell_dbc` WHERE `ID` = 372608;
UPDATE `_mod_skyriding_spell_template` SET
    `ID` = 404464,
    `Attributes` = `Attributes` | 64,
    `RecoveryTime` = 0,
    `Effect_1` = 6,
    `EffectAura_1` = 4,
    `DurationIndex` = 21,
    `Name_Lang_enUS` = 'Flight Style: Skyriding',
    `NameSubtext_Lang_enUS` = '',
    `Description_Lang_enUS` = 'Skyriding is currently enabled.';
INSERT INTO `spell_dbc` SELECT * FROM `_mod_skyriding_spell_template`;
DELETE FROM `_mod_skyriding_spell_template`;

INSERT INTO `_mod_skyriding_spell_template`
SELECT * FROM `spell_dbc` WHERE `ID` = 372608;
UPDATE `_mod_skyriding_spell_template` SET
    `ID` = 404468,
    `Attributes` = `Attributes` | 64,
    `RecoveryTime` = 0,
    `Effect_1` = 6,
    `EffectAura_1` = 4,
    `DurationIndex` = 21,
    `Name_Lang_enUS` = 'Flight Style: Steady',
    `NameSubtext_Lang_enUS` = '',
    `Description_Lang_enUS` = 'Steady Flight is currently enabled.';
INSERT INTO `spell_dbc` SELECT * FROM `_mod_skyriding_spell_template`;
DELETE FROM `_mod_skyriding_spell_template`;

-- Retail 404471 is a grounded two-second cast. CastingTimeIndex 5 is the
-- 2,000 ms entry in the 3.3.5 SpellCastTimes table.
INSERT INTO `_mod_skyriding_spell_template`
SELECT * FROM `spell_dbc` WHERE `ID` = 372608;
UPDATE `_mod_skyriding_spell_template` SET
    `ID` = 404471,
    `CastingTimeIndex` = 5,
    `RecoveryTime` = 1500,
    `Name_Lang_enUS` = 'Change Flight Style',
    `NameSubtext_Lang_enUS` = '',
    `Description_Lang_enUS` = 'Switch between Skyriding and Steady Flight. Must be grounded.';
INSERT INTO `spell_dbc` SELECT * FROM `_mod_skyriding_spell_template`;

DROP TEMPORARY TABLE `_mod_skyriding_spell_template`;
