-- Add the Vigor aura required by the native WXL Skyriding state contract.
-- Keep this as a WotLK-compatible dummy aura; mod-skyriding owns charges.
DELETE FROM `spell_dbc` WHERE `ID` = 372773;

DROP TEMPORARY TABLE IF EXISTS `_mod_skyriding_vigor_template`;
CREATE TEMPORARY TABLE `_mod_skyriding_vigor_template` LIKE `spell_dbc`;

INSERT INTO `_mod_skyriding_vigor_template`
SELECT * FROM `spell_dbc` WHERE `ID` = 372608;

UPDATE `_mod_skyriding_vigor_template` SET
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

INSERT INTO `spell_dbc`
SELECT * FROM `_mod_skyriding_vigor_template`;

DROP TEMPORARY TABLE `_mod_skyriding_vigor_template`;
