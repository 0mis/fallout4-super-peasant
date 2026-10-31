"""Fallout 4 record-layout knowledge shared by build_esp.py and validate_esp.py (agent B2). Stdlib only.

Everything here was checked on this PC (2026-10-09) against one of:
  [esm]   Fallout4.esm 1.11.240 data (work/plugin/scratch/scan*.py: statistics over every vanilla record of the type)
  [xedit] the flag/enum name tables compiled into tools/xedit/xFOEdit64.exe 4.1.5f (UTF-16 literals, in bit order)
  [xdump] xEdit's own decoding of our built plugin (work/plugin/xdump: every flag/enum below shows the intended name)
Each constant names its source. Offsets are byte offsets inside the subrecord.
"""
import struct

# ------------------------------------------------------------------ strings
# Localized-string subrecords per record type (Fallout4.esm holds 4-byte string IDs there). esp_writer.LSTRING_SIGS
# plus ALCH DNAM ("Addiction Name": its 4 bytes 0x00023FAF in DogWhistle are no form of the right type [esm]).
LSTRING_SIGS = {
    "NPC_": ["FULL", "SHRT", "ATTX"],
    "QUST": ["FULL", "NNAM"],
    "ARMO": ["FULL", "DESC"],
    "MGEF": ["FULL", "DNAM"],
    "SPEL": ["FULL", "DESC"],
    "ALCH": ["FULL", "DESC", "DNAM"],
    "MESG": ["FULL", "DESC", "ITXT"],
    "PERK": ["FULL", "DESC", "EPF2"],
    "FLST": ["FULL"],
    "EXPL": ["FULL"],
}

# ------------------------------------------------------------------ QUST
# DNAM = flags:u16 priority:u8 formVersion:u8 unknown:u32 type:u32 (12 bytes) [esm]
QUST_FLAGS = {  # [xedit] FO4 list, bit order
    "Start Game Enabled": 0x0001, "Completed": 0x0002, "Add Idle topic to Hello": 0x0004,
    "Allow repeated stages": 0x0008, "Starts Enabled": 0x0010, "Displayed In HUD": 0x0020, "Failed": 0x0040,
    "Stage Wait": 0x0080, "Run Once": 0x0100, "Exclude from dialogue export": 0x0200,
    "Warn on alias fill failure": 0x0400, "Active": 0x0800, "Repeats Conditions": 0x1000, "Keep Instance": 0x2000,
    "Want Dormant": 0x4000, "Has Dialogue Data": 0x8000,
}
# Every Start Game Enabled quest in Fallout4.esm also has 0x0010 "Starts Enabled" (0x11/0x111/0x311/... ; 0x0001
# alone never occurs) [esm], so the builder sets both.
QUST_SGE_COMPANION = 0x0010
QUST_DNAM_PRIORITY_OFS = 2

ALIAS_FLAGS = {  # [xedit] FO4 quest alias flags, bit order (FNAM u32)
    "Reserves Location/Reference": 1 << 0, "Optional": 1 << 1, "Quest Object": 1 << 2,
    "Allow Reuse in Quest": 1 << 3, "Allow Dead": 1 << 4, "Matching Ref - In Loaded Area": 1 << 5,
    "Essential": 1 << 6, "Allow Disabled": 1 << 7, "Stores Text": 1 << 8, "Allow Reserved": 1 << 9,
    "Protected": 1 << 10, "Forced by Aliases": 1 << 11, "Allow Destroyed": 1 << 12,
    "Matching Ref - Closest": 1 << 13, "Uses Stored Text": 1 << 14, "Initially Disabled": 1 << 15,
    "Allow Cleared": 1 << 16, "Clear Names When Removed": 1 << 17, "Matching Ref - Actors Only": 1 << 18,
    "Create Ref - Temp": 1 << 19, "External Alias - Linked": 1 << 20, "No Pickpocket": 1 << 21,
    "Can Apply Data To Non-Aliased Refs": 1 << 22, "Is Companion": 1 << 23, "Optional All Scenes": 1 << 24,
}
# Reference alias subrecord order (vanilla: ALST ALID FNAM [fill] ... ALPC* VTCK ALED) [esm]
ALIAS_FILL_SIGS = ("ALFI", "ALFL", "ALFR", "ALUA", "ALFA", "KNAM", "ALRT", "ALEQ", "ALEA", "ALCO", "ALCA", "ALCL",
                   "ALNA", "ALNT", "ALFE", "ALFD", "ALCC")
QUST_STAGE_OBJECTIVE_SIGS = ("INDX", "QSDT", "CNAM", "NAM2", "NAM3", "SCHR", "QOBJ", "QSTA", "NNAM")

# ------------------------------------------------------------------ NPC_
# ACBS = flags:u32 xpOffset:i16 level:i16 calcMin:u16 calcMax:u16 disposition:i16 templateFlags:u16
#        bleedout:i16 unknown:u16 (20 bytes) [esm]
ACBS_FLAGS = {  # [xedit] (same low bits as TES5)
    "Female": 0x1, "Essential": 0x2, "Is CharGen Face Preset": 0x4, "Respawn": 0x8, "Auto-calc stats": 0x10,
    "Unique": 0x20, "Doesn't affect stealth meter": 0x40, "PC Level Mult": 0x80, "Protected": 0x800,
    "Summonable": 0x4000, "Doesn't bleed": 0x10000, "Bleedout Override": 0x40000,
    "Opposite Gender Anims": 0x80000, "Simple Actor": 0x100000, "Is Ghost": 0x20000000,
    "Invulnerable": 0x80000000,
}
ACBS_LEVEL_OFS = 6
ACBS_TEMPLATE_FLAGS_OFS = 14
# AIDT (24 bytes): aggression:u8 confidence:u8 energy:u8 responsibility:u8 mood:u8 assistance:u8 ... [esm]
AIDT_AGGRESSION = {"Unaggressive": 0, "Aggressive": 1, "Very Aggressive": 2, "Frenzied": 3}
AIDT_CONFIDENCE = {"Cowardly": 0, "Cautious": 1, "Average": 2, "Brave": 3, "Foolhardy": 4}
AIDT_ASSISTANCE = {"Helps Nobody": 0, "Helps Allies": 1, "Helps Friends and Allies": 2}
AIDT_OFS = {"aggression": 0, "confidence": 1, "assistance": 5}
# SNAM faction = formid:u32 rank:i8 (5 bytes); PRKR = formid:u32 rank:u8 (5 bytes, 2771/2771 vanilla) [esm]
# PRKZ/PRKR sit right before PRPS (807+430+71+... vanilla NPCs) [esm]

# ------------------------------------------------------------------ ARMO / ARMA
# BOD2 is a single u32 of biped slots (bit n = slot 30+n) in both ARMO and ARMA (4 bytes in every template) [esm]
ARMO_FLAG_NON_PLAYABLE = 0x4      # record header flag
# ARMO DATA = value:i32 weight:f32 health:u32 (12 bytes) [esm]
# ARMA: MOD2 male / MOD3 female biped model, MO2T/MO3T texture hashes, MO2F/MO3F flags, MO2S/MO3S material swaps,
#       MOD4/MOD5 first person; MODL = additional race [esm order: EDID BOD2 RNAM DNAM MOD2 MO2T MOD3 MO3T ...]
ARMA_MODEL_FAMILY = ("MOD2", "MO2T", "MO2C", "MO2F", "MO2S", "MOD3", "MO3T", "MO3C", "MO3F", "MO3S",
                     "MOD4", "MO4T", "MO4C", "MO4F", "MO4S", "MOD5", "MO5T", "MO5C", "MO5F", "MO5S")

# ------------------------------------------------------------------ CTDA (32 bytes) [esm]
# op:u8 (operator in bits 5-7: 0 ==, 1 !=, 2 >, 3 >=, 4 <, 5 <=; 0x01 OR, 0x04 use global) pad[3]
# value:f32 function:u16 pad:u16 param1:u32 param2:u32 runOn:u32 reference:u32 param3:i32(-1)
CTDA = struct.Struct("<B3sfHHIIIIi")
CTDA_OPS = {"==": 0, "!=": 1, ">": 2, ">=": 3, "<": 4, "<=": 5}
CTDA_RUN_ON = {"Subject": 0, "Target": 1, "Reference": 2, "Combat Target": 3, "Linked Reference": 4,
               "Quest Alias": 5, "Package Data": 6, "Event Data": 7}
FUNC_GET_GLOBAL_VALUE = 74        # vanilla CTDAs with function 0x4A carry a GLOB in param1 [esm]


def ctda_bytes(function, param1, op="==", value=0.0, run_on="Subject", param2=0, reference=0, param3=-1):
    return CTDA.pack(CTDA_OPS[op] << 5, b"\0\0\0", value, function, 0, param1, param2, CTDA_RUN_ON[run_on],
                     reference, param3)


def ctda_decode(data):
    op, _pad, value, fn, _pad2, p1, p2, run_on, ref, p3 = CTDA.unpack(data)
    return {"op_byte": op, "op": op >> 5, "value": value, "function": fn, "param1": p1, "param2": p2,
            "run_on": run_on, "reference": ref, "param3": p3}


# ------------------------------------------------------------------ PACK [esm]
# PKCU = dataInputCount:u32 packageTemplate:formid versionCount:u32
# PLDT = type:i32 value:u32 radius:i32 collectionIndex:i32 ; types used here:
PLDT_TYPES = {"Near reference": 0, "In cell": 1, "Near current location": 2, "Near editor location": 3,
              "Object ID": 4, "Object Type": 5, "Near linked reference": 6, "At package location": 7,
              "Alias (reference)": 8, "Alias (location)": 9, "Near self": 12}
PLDT = struct.Struct("<iIii")

# ------------------------------------------------------------------ MGEF (DATA = 152 bytes) [esm]
MGEF_FLAGS = {  # [xedit]-style FO4 names (TES5 layout); value 0x1C008010 for SP_PunchEffect
    "Hostile": 0x1, "Recover": 0x2, "Detrimental": 0x4, "Snap to Navmesh": 0x8, "No Hit Event": 0x10,
    "Dispel with Keywords": 0x100, "No Duration": 0x200, "No Magnitude": 0x400, "No Area": 0x800,
    "FX Persist": 0x1000, "Gory Visuals": 0x4000, "Hide in UI": 0x8000, "No Recast": 0x20000,
    "Power Affects Magnitude": 0x200000, "Power Affects Duration": 0x400000, "Painless": 0x4000000,
    "No Hit Effect": 0x8000000, "No Death Dispel": 0x10000000,
}
MGEF_ARCHETYPE = {"Value Modifier": 0, "Script": 1, "Dispel": 2, "Cure Disease": 3}
CASTING_TYPE = {"Constant Effect": 0, "Fire and Forget": 1, "Concentration": 2, "Scroll": 3}
DELIVERY = {"Self": 0, "Touch": 1, "Aimed": 2, "Target Actor": 3, "Target Location": 4}
MGEF_OFS = {"flags": 0, "base_cost": 4, "assoc_item": 8, "resist_av": 16, "archetype": 64, "actor_value": 68,
            "projectile": 72, "explosion": 76, "casting_type": 80, "delivery": 84, "second_av": 88}
# both templates: archetype 1 / casting 1 / delivery 3 (ReconScopeEffect) or 0 (JoelDogWhistleEffect) [esm]

# ------------------------------------------------------------------ SPEL (SPIT = 36 bytes) [esm]
# cost:u32 flags:u32 type:u32 chargeTime:f32 castType:u32 targetType:u32 castDuration:f32 range:f32 perk:formid
SPIT = struct.Struct("<IIIfIIffI")
# FO4 bit positions differ from TES5 (xEdit FO4 table: bit 0 Manual Cost Calc, 1..16 Unknown 2..17, 17 PC Start Spell,
# 18 Instant Cast, 19 AE Ignores LOS, 20 Ignore Resistance, 21 No Absorb/Reflect, 22 Unknown 23, 23 No Dual Cast
# Modification). [xedit] string order + [xdump]: 0xC00001 decoded as "Manual Cost Calc, Unknown 23, No Dual Cast
# Modification", 0x300001 as "Manual Cost Calc, Ignore Resistance, No Absorb/Reflect".
SPIT_FLAGS = {"Manual Cost Calc": 0x1, "PC Start Spell": 0x20000, "Instant Cast": 0x40000,
              "Area Effect Ignores LOS": 0x80000, "Ignore Resistance": 0x100000, "No Absorb/Reflect": 0x200000,
              "No Dual Cast Modification": 0x800000}
SPELL_TYPE = {"Spell": 0, "Disease": 1, "Power": 2, "Lesser Power": 3, "Ability": 4, "Poison": 5}
# EFID = MGEF formid ; EFIT = magnitude:f32 area:u32 duration:u32
EFIT = struct.Struct("<fII")

# ------------------------------------------------------------------ ALCH [esm]
# DATA = weight:f32 ; ENIT = value:i32 flags:u32 addiction:formid addictionChance:f32 consumeSound:formid
ENIT = struct.Struct("<iIIfI")

# ------------------------------------------------------------------ MESG [esm]
# DNAM flags: 0x1 Message Box, 0x2 Delay Initial Display ; buttons = ITXT (+ CTDA*) each
MESG_FLAG_MESSAGE_BOX = 0x1

# ------------------------------------------------------------------ GLOB [esm]
# EDID [FNAM type char] FLTV f32 ; FNAM in vanilla: 's' 743, absent 562, 'f' 40, 'l' 1 ; record flag 0x40 = Constant
GLOB_FLAG_CONSTANT = 0x40

# ------------------------------------------------------------------ PERK [esm, xedit]
# DATA (5) = trait level numRanks playable hidden ; entry = PRKE(type 2=entry point, rank, priority)
# DATA(entryPoint, function, conditionTabCount) [PRKC + CTDA*]* EPFT EPFB(entry id u16) EPFD PRKF
PERK_ENTRY_POINTS = {"Mod Incoming Stagger": 33, "Mod Falling Damage": 58}   # [xedit] enum order, 58 = template
PERK_ENTRY_TABS = {33: 2, 58: 1}           # vanilla mod_armor_LessStagger: 21 02 02 ; crNoFallDamage: 3a 03 01 [esm]
PERK_FUNCTIONS = {"Set Value": 1, "Add Value": 2, "Multiply Value": 3}

# ------------------------------------------------------------------ EXPL (DATA = 84 bytes) [esm]
# light sound1 sound2 impactDataSet placedObject spawnProjectile : formids ; force damage innerRadius outerRadius
# isRadius verticalOffsetMult : f32 ; flags:u32 soundLevel:u32 placedObjectFadeDelay:f32 stagger:u32 ; spawn x y z
# spreadDeg:f32 count:u32
EXPL_OFS = {"light": 0, "sound1": 4, "sound2": 8, "impact": 12, "placed": 16, "projectile": 20, "force": 24,
            "damage": 28, "inner_radius": 32, "outer_radius": 36, "is_radius": 40, "flags": 48, "stagger": 60}
EXPL_FLAG_KNOCKDOWN_ALWAYS = 0x4
EXPL_FLAG_KNOCKDOWN_FORMULA = 0x8
EXPL_STAGGER = {"None": 0, "Small": 1, "Medium": 2, "Large": 3, "Extra Large": 4}

# ------------------------------------------------------------------ SNDR (sound descriptor) [esm, xedit]
# Fallout4.esm 1.11.240: 5475 SNDR; 4469 have exactly EDID CNAM GNAM ANAM* ONAM LNAM BNAM (527 more add CTDA before
# LNAM, 65 add SNAM after GNAM). ANAM = one zero-terminated sound file path per subrecord, ALL 10085 end in ".wav"
# (the engine falls back to the archived .xwm), 9810 start "data\Sound\" (e.g. WPNSwingUnarmedA:
# "data\Sound\FX\WPN\Unarmed\wpn_swing_unarmed_02.wav"). Several ANAM = the engine picks one at random per play.
# CNAM = descriptor type as a 32-bit hash: Standard 0x1EEF540A (one-shot/loop with files), Compound 0x54651A43 (DNAM
#        list of child SNDRs, no files), AutoWeapon (ITM* + FNAM). Only Standard is written here.
# GNAM = SNCT category ; SNAM = alternate sound for (SNDR) ; ONAM = SOPM output model (3D attenuation: SOMMono04000
#        ATTN = min 400 u, max 4000 u) ; DNAM = child SNDR (compound only)
# LNAM (4 bytes) = unknown:u8 looping:u8 unknown:u8 rumble:u8 ; looping byte over the 5065 vanilla LNAM: 0x00 4358,
#        0x08 414 ("...LP" names), 0x10 283 ("...LPM"), 0x20 10 ; xEdit: None / Loop / Envelope Fast / Envelope Slow
# BNAM (6 bytes) = frequencyShift%:s8 frequencyVariance%:s8 priority:u8 dbVariance:u8 staticAttenuation:u16 (dB*100)
#        e.g. WPNSwingUnarmedA 00 0a 40 03 3206 = shift 0, variance 10 %, priority 64, dB variance 3, 15.86 dB
# Papyrus 'Sound' is the SNDR form: vanilla Followers quest VMAD property FollowersScript.NPCHumanLockpickingPickBreak
# (type Sound) = 0x001694D5, an SNDR [esm]; so Sound.Play(akSource) needs no SOUN (sound marker) record.
SNDR_CNAM = {"Standard": 0x1EEF540A, "Compound": 0x54651A43}
SNDR_LOOPING = {"None": 0x00, "Loop": 0x08, "Envelope Fast": 0x10, "Envelope Slow": 0x20}
SNDR_LNAM = struct.Struct("<BBBB")
SNDR_BNAM = struct.Struct("<bbBBH")
SNDR_ORDER = ("EDID", "CNAM", "GNAM", "SNAM", "ANAM", "ONAM", "CTDA", "LNAM", "BNAM")

# ------------------------------------------------------------------ references
# FormID fields of the subrecords that can occur in our records: sig -> [(offset, stride or None, allowed sigs)].
# stride: the field repeats every `stride` bytes to the end of the subrecord. allowed None = any record type.
# Engine-defined forms with no record in Fallout4.esm:
ENGINE_FORMS = {0x00000014: "ACHR"}     # PlayerRef
REF_SIGS = {"REFR", "ACHR", "PGRE", "PMIS", "PHZD", "PARW", "PBAR", "PBEA", "PCON", "PFLA"}
FORMID_FIELDS = {
    "QUST": {"ALFR": [(0, None, REF_SIGS)], "ALUA": [(0, None, {"NPC_"})], "ALPC": [(0, None, {"PACK"})],
             "ALDN": [(0, None, {"MESG"})], "ALFL": [(0, None, {"LCTN"})], "ALEQ": [(0, None, {"QUST"})],
             "VTCK": [(0, None, {"NPC_", "FACT", "FLST", "VTYP"})], "ALSP": [(0, None, {"SPEL"})],
             "ALFC": [(0, None, {"FACT"})]},
    "NPC_": {"SNAM": [(0, None, {"FACT"})], "VTCK": [(0, None, {"VTYP"})], "RNAM": [(0, None, {"RACE"})],
             "CNAM": [(0, None, {"CLAS"})], "DOFT": [(0, None, {"OTFT"})], "SOFT": [(0, None, {"OTFT"})],
             "PNAM": [(0, None, {"HDPT"})], "HCLF": [(0, None, {"CLFM"})], "FTST": [(0, None, {"TXST"})],
             "PRKR": [(0, None, {"PERK"})], "PRPS": [(0, 8, {"AVIF"})], "CNTO": [(0, None, None)],
             "PKID": [(0, None, {"PACK"})], "TPLT": [(0, None, {"NPC_", "LVLN"})], "ZNAM": [(0, None, {"CSTY"})],
             "INAM": [(0, None, {"LVLI"})], "PTRN": [(0, None, {"TRNS"})], "KWDA": [(0, 4, {"KYWD"})],
             "CRIF": [(0, None, {"FACT"})], "DPLT": [(0, None, {"FLST"})], "WNAM": [(0, None, {"ARMO"})]},
    "ARMA": {"RNAM": [(0, None, {"RACE"})], "MODL": [(0, None, {"RACE"})], "ONAM": [(0, None, {"ARTO"})]},
    "ARMO": {"RNAM": [(0, None, {"RACE"})], "MODL": [(0, None, {"ARMA"})], "EITM": [(0, None, {"ENCH"})],
             "PTRN": [(0, None, {"TRNS"})], "KWDA": [(0, 4, {"KYWD"})], "YNAM": [(0, None, {"SNDR"})],
             "ZNAM": [(0, None, {"SNDR"})], "ETYP": [(0, None, {"EQUP"})], "TNAM": [(0, None, {"ARMO"})]},
    "OTFT": {"INAM": [(0, 4, {"ARMO", "LVLI"})]},
    "PACK": {"PKCU": [(4, None, {"PACK"})]},
    "MGEF": {"DATA": [(8, None, None), (16, None, {"AVIF"}), (24, None, {"LIGH"}), (32, None, {"EFSH"}),
                      (36, None, {"EFSH"}), (68, None, {"AVIF"}), (72, None, {"PROJ"}), (76, None, {"EXPL"}),
                      (88, None, {"AVIF"}), (92, None, {"ARTO"}), (96, None, {"ARTO"}), (100, None, {"IPDS"}),
                      (116, None, {"ARTO"}), (120, None, {"RFCT"}), (124, None, {"RFCT"}),
                      (128, None, {"SPEL"}), (132, None, {"IMAD"}), (136, None, {"PERK"})],
             "KWDA": [(0, 4, {"KYWD"})], "SNDD": [(4, 8, {"SNDR"})]},
    "SPEL": {"ETYP": [(0, None, {"EQUP"})], "EFID": [(0, None, {"MGEF"})], "SPIT": [(32, None, {"PERK"})],
             "KWDA": [(0, 4, {"KYWD"})]},
    "ALCH": {"PTRN": [(0, None, {"TRNS"})], "YNAM": [(0, None, {"SNDR"})], "ZNAM": [(0, None, {"SNDR"})],
             "ETYP": [(0, None, {"EQUP"})], "EFID": [(0, None, {"MGEF"})],
             "ENIT": [(8, None, {"SPEL"}), (16, None, {"SNDR"})], "KWDA": [(0, 4, {"KYWD"})]},
    "MESG": {"QNAM": [(0, None, {"QUST"})]},
    "FLST": {"LNAM": [(0, None, None)]},
    "PERK": {"NNAM": [(0, None, {"PERK"})]},
    "EXPL": {"DATA": [(0, None, {"LIGH"}), (4, None, {"SNDR"}), (8, None, {"SNDR"}), (12, None, {"IPDS"}),
                      (16, None, None), (20, None, {"PROJ"})],
             "MNAM": [(0, None, {"IMAD"})], "EITM": [(0, None, {"ENCH"})]},
    "SNDR": {"GNAM": [(0, None, {"SNCT"})], "SNAM": [(0, None, {"SNDR"})], "ONAM": [(0, None, {"SOPM"})],
             "DNAM": [(0, None, {"SNDR"})]},
}
# MODL is a FormID only in ARMA/ARMO (race / armature); elsewhere it is a model path.
MODEL_PATH_SIGS = ("MODL", "MOD2", "MOD3", "MOD4", "MOD5")
MODEL_PATH_FORMID_TYPES = {"ARMA": {"MODL"}, "ARMO": {"MODL"}}

# ------------------------------------------------------------------ Papyrus types -> record signatures
PAPYRUS_FORM_TYPES = {
    "form": None, "actorbase": {"NPC_"}, "actor": {"ACHR"}, "objectreference": REF_SIGS,
    "spell": {"SPEL"}, "outfit": {"OTFT"}, "potion": {"ALCH"}, "globalvariable": {"GLOB"}, "action": {"AACT"},
    "actorvalue": {"AVIF"}, "worldspace": {"WRLD"}, "location": {"LCTN"}, "formlist": {"FLST"},
    "message": {"MESG"}, "explosion": {"EXPL"}, "static": {"STAT"}, "keyword": {"KYWD"},
    "magiceffect": {"MGEF"}, "perk": {"PERK"}, "package": {"PACK"}, "armor": {"ARMO"}, "race": {"RACE"},
    "faction": {"FACT"}, "quest": {"QUST"}, "cell": {"CELL"}, "idle": {"IDLE"}, "weapon": {"WEAP"},
    "miscobject": {"MISC"}, "leveledactor": {"LVLN"}, "leveleditem": {"LVLI"},
    "sound": {"SNDR"},  # [esm] vanilla Sound properties hold SNDR forms (see SNDR above)
    "activator": {"ACTI"}, "container": {"CONT"}, "door": {"DOOR"}, "furniture": {"FURN"}, "light": {"LIGH"},
    "projectile": {"PROJ"}, "class": {"CLAS"}, "combatstyle": {"CSTY"}, "encounterzone": {"ECZN"},
    "headpart": {"HDPT"}, "imagespacemodifier": {"IMAD"}, "impactdataset": {"IPDS"}, "voicetype": {"VTYP"},
    "topic": {"DIAL"}, "topicinfo": {"INFO"}, "scene": {"SCEN"}, "book": {"BOOK"}, "ammo": {"AMMO"},
    "holotape": {"NOTE"}, "terminal": {"TERM"}, "visualeffect": {"RFCT"}, "objectmod": {"OMOD"},
    "constructibleobject": {"COBJ"}, "enchantment": {"ENCH"}, "hazard": {"HAZD"}, "ingredient": {"INGR"},
    "key": {"KEYM"}, "locationreftype": {"LCRT"}, "musictype": {"MUSC"}, "outputmodel": {"SOPM"},
    "shaderparticlegeometry": {"SPGD"}, "soundcategory": {"SNCT"}, "textureset": {"TXST"}, "weather": {"WTHR"},
    "effectshader": {"EFSH"}, "associationtype": {"ASTP"}, "camerashot": {"CAMS"}, "equipslot": {"EQUP"},
    "instancenamingrules": {"INNR"}, "movablestatic": {"MSTT"}, "wordofpower": {"WOOP"}, "talkingactivator": {"TACT"},
}
PAPYRUS_ALIAS_TYPES = {"referencealias": "ALST", "locationalias": "ALLS", "refcollectionalias": "ALCS"}
PAPYRUS_PRIMITIVES = {"int", "float", "bool", "string", "var"}
