/*
 * Star Fox 64: Recompiled — Randomizer
 * Free & open-source. Seed-deterministisch (speedrun-tauglich).
 *
 * Credits / Inspiration:
 *   - punk7890           : Original Star Fox 64 ASM-Randomizer (Feature-Blaupause)
 *   - sonicdcer          : N64-Recomp-Port, sf64-Decompilation & Mod-Template
 *
 * Phase 2a: Random Music (geseedet, reproduzierbar).
 */
#include "modding.h"
#include "recomputils.h"
#include "recompconfig.h"

#include "context.h"
#include "functions.h"
#include "sfx.h"
#include "bgm.h"
#include "audioseq_cmd.h"
#include "fox_map.h"

void Audio_ClearVoice(void);

/* =========================================================================
 *  Seed-deterministischer RNG
 *  Wichtig fuer Speedrunning: gleicher Seed -> exakt gleicher Run.
 *  Wir nutzen KEINE Frame-Timer (anders als der ASM-Original-Randomizer),
 *  sondern ausschliesslich den Seed.
 * ========================================================================= */

/* FNV-1a Hash: macht aus einem Seed-String (z.B. "RUN-123") einen u32. */
static u32 Rando_HashSeed(const char* s) {
    u32 h = 2166136261u;
    if (s != NULL) {
        while (*s != '\0') {
            h ^= (u8) (*s++);
            h *= 16777619u;
        }
    }
    return h;
}

/* xorshift32 — kleiner, schneller, deterministischer PRNG. */
static u32 sRngState;

static void Rando_RngSeed(u32 seed) {
    sRngState = (seed != 0u) ? seed : 0xDEADBEEFu; /* 0 ist ein toter Zustand */
}

static u32 Rando_RngNext(void) {
    u32 x = sRngState;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    sRngState = x;
    return x;
}

/* Liest das Seed-Feld aus der Mod-Config und hasht es. */
static u32 Rando_ReadSeed(void) {
    char* s = recomp_get_config_string("seed");
    u32 h = Rando_HashSeed(s);
    if (s != NULL) {
        recomp_free_config_string(s);
    }
    return h;
}

/* =========================================================================
 *  Feature: Random Music
 *  Wir mischen die 15 Haupt-Stage-Themes per geseedeter Permutation
 *  (Fisher-Yates). Jedes Stage-Theme wird deterministisch auf ein anderes
 *  abgebildet. Boss-/Menue-/Cutscene-Musik bleibt unangetastet, damit das
 *  Spiel sich richtig anfuehlt und durchspielbar bleibt.
 * ========================================================================= */

static const u16 kStagePool[] = {
    SEQ_ID_CORNERIA, SEQ_ID_METEO,   SEQ_ID_TITANIA,  SEQ_ID_SECTOR_X,
    SEQ_ID_ZONESS,   SEQ_ID_AREA_6,  SEQ_ID_VENOM_1,  SEQ_ID_SECTOR_Y,
    SEQ_ID_FORTUNA,  SEQ_ID_SOLAR,   SEQ_ID_BOLSE,    SEQ_ID_KATINA,
    SEQ_ID_AQUAS,    SEQ_ID_SECTOR_Z, SEQ_ID_MACBETH,
};
#define STAGE_POOL_N ((u32) (sizeof(kStagePool) / sizeof(kStagePool[0])))

/* Salt, damit dieses Feature einen eigenen RNG-Strom hat (nicht mit
 * spaeteren Features korreliert). "MUS1" als Konstante. */
#define MUSIC_SALT 0x4D555331u

static u8 sMusicPerm[STAGE_POOL_N];
static u32 sBuiltSeed;
static u8 sPermBuilt = 0;

static void Rando_BuildMusicPerm(u32 seed) {
    u32 i;
    for (i = 0; i < STAGE_POOL_N; i++) {
        sMusicPerm[i] = (u8) i;
    }
    Rando_RngSeed(seed ^ MUSIC_SALT);
    for (i = STAGE_POOL_N - 1u; i > 0u; i--) {
        u32 j = Rando_RngNext() % (i + 1u);
        u8 tmp = sMusicPerm[i];
        sMusicPerm[i] = sMusicPerm[j];
        sMusicPerm[j] = tmp;
    }
    sBuiltSeed = seed;
    sPermBuilt = 1;
}

/* Bildet eine Stage-Theme-SeqId auf die geshuffelte ab. Andere IDs bleiben. */
static u16 Rando_RemapBgm(u16 seqId) {
    u16 base = seqId & ~SEQ_FLAG; /* Flag (0x8000) abstreifen */
    u16 flag = seqId & SEQ_FLAG;
    u32 i;
    for (i = 0; i < STAGE_POOL_N; i++) {
        if (kStagePool[i] == base) {
            u32 seed = Rando_ReadSeed();
            if (!sPermBuilt || (seed != sBuiltSeed)) {
                Rando_BuildMusicPerm(seed);
            }
            return (u16) (kStagePool[sMusicPerm[i]] | flag);
        }
    }
    return seqId;
}

/* enum_random_music: 0 = Enabled (Default), 1 = Disabled */
static int Rando_MusicEnabled(void) {
    return recomp_get_config_u32("enum_random_music") == 0;
}

/*
 * Patch der winzigen Original-Funktion (2 Zeilen, aus der Decomp uebernommen).
 * Wir haengen nur unsere Remap-Zeile davor.
 */
RECOMP_PATCH void Audio_PlaySequence(u8 seqPlayId, u16 seqId, u8 fadeinTime, u8 bgmParam) {
    if ((seqPlayId == SEQ_PLAYER_BGM) && Rando_MusicEnabled()) {
        seqId = Rando_RemapBgm(seqId);
    }
    SEQCMD_SET_SEQPLAYER_IO(seqPlayId, 0, bgmParam);
    SEQCMD_PLAY_SEQUENCE(seqPlayId, fadeinTime, 0, seqId);
}

/* =========================================================================
 *  QoL: Level-Intros immer skippbar
 *
 *  Das Spiel KANN das Level-Intro per START ueberspringen (in Play_Main,
 *  direkt nach Play_Update). Aber nur, wenn der Planet schon mal normal
 *  gecleart wurde (gSaveFile...normalClear). Beim ersten Durchgang geht es
 *  daher nicht. Wir replizieren genau die Original-Skip-Routine OHNE diese
 *  Bedingung und "verbrauchen" danach den START-Druck, damit der eingebaute
 *  Check + das Pausemenue nicht im selben Frame zusaetzlich feuern.
 *
 *  Speedrun-Hinweis: rein input-getrieben, beeinflusst den Seed/RNG nicht.
 * ========================================================================= */
static int Rando_SkipIntrosEnabled(void) {
    return recomp_get_config_u32("enum_skip_intros") == 0;
}

RECOMP_HOOK_RETURN("Play_Update") void Rando_SkipLevelIntro(void) {
    s32 i;

    if (!Rando_SkipIntrosEnabled()) {
        return;
    }
    if (!(gControllerPress[gMainController].button & START_BUTTON) ||
        (gPlayer[0].state != PLAYERSTATE_LEVEL_INTRO)) {
        return;
    }

    /* 1:1 die Original-Skip-Routine aus Play_Main (fox_play.c). */
    Audio_ClearVoice();
    Audio_SetEnvSfxReverb(0);
    Play_ClearObjectData();

    for (i = 0; i < gCamCount; i++) {
        Audio_KillSfxBySource(gPlayer[i].sfxSource);
        Audio_StopPlayerNoise(i);
    }

    gPlayState = PLAY_INIT;
    gDrawMode = gVersusMode = 0;
    gCamCount = 1;
    gBgColor = 0;
    gCsFrameCount = gLevelClearScreenTimer = gLevelStartStatusScreenTimer = gRadioState = 0;
    /* Hinweis: Original setzt hier 'gCsWasNotSkipped = false;'. Das Symbol ist
     * nicht in den Referenz-Symbolen exportiert, daher ausgelassen. Folge: nur
     * minimaler kosmetischer Unterschied beim erstmaligen Skip von Solar
     * (Hitzealarm) / Aquas (Umgebung). Gameplay unberuehrt. */

    /* START verbrauchen -> kein doppelter Skip / kein versehentliches Pause. */
    gControllerPress[gMainController].button &= ~START_BUTTON;
}

/* =========================================================================
 *  QoL: Mission-Briefing (General Pepper, nach Planetenwahl) immer skippbar
 *
 *  Im Briefing (Map_LevelStart_Update) haengen START-Skip UND A-Text-Speedup
 *  an der Bedingung !sLevelPlayed. sLevelPlayed wird aus
 *  Map_LevelPlayedStatus_Check() gesetzt — und diese Funktion ist INVERTIERT:
 *  sie gibt beim ERSTEN Durchgang 'true' zurueck (= Skip gesperrt) und erst
 *  bei Wiederholung 'false' (= Skip frei). Daher beim ersten Mal kein Skip.
 *
 *  Die Funktion wird im ganzen Spiel NUR an dieser einen Stelle aufgerufen.
 *  Wenn unser QoL aktiv ist, geben wir immer 'false' zurueck -> START-Skip
 *  und A-Speedup sofort frei. Sonst exaktes Original-Verhalten.
 * ========================================================================= */
RECOMP_PATCH bool Map_LevelPlayedStatus_Check(PlanetId planet) {
    u32 planetSaveSlot;
    s32 played = true;

    if (Rando_SkipIntrosEnabled()) {
        return false; /* Briefing immer skip-/speedup-bar */
    }

    /* --- ab hier 1:1 Original --- */
    switch (planet) {
        case PLANET_METEO:
        case PLANET_AREA_6:
        case PLANET_BOLSE:
        case PLANET_SECTOR_Z:
        case PLANET_SECTOR_X:
        case PLANET_SECTOR_Y:
        case PLANET_KATINA:
        case PLANET_MACBETH:
        case PLANET_ZONESS:
        case PLANET_CORNERIA:
        case PLANET_TITANIA:
        case PLANET_AQUAS:
        case PLANET_FORTUNA:
            planetSaveSlot = planet;
            break;
        case PLANET_SOLAR:
            planetSaveSlot = SAVE_SLOT_SOLAR;
            break;
        default:
            planetSaveSlot = planet;
            break;
    }

    if (gSaveFile.save.data.planet[planetSaveSlot].played & 1) {
        played = false;
    }
    return played;
}
