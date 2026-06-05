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
#include "sys.h"
#include "sf64thread.h"
#include "sfx.h"
#include "bgm.h"
#include "audioseq_cmd.h"
#include "fox_map.h"
#include "sf64level.h"
#include "sf64object.h"

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
static u32 Rando_ReadSeedConfig(void) {
    char* s = recomp_get_config_string("seed");
    u32 h = Rando_HashSeed(s);
    if (s != NULL) {
        recomp_free_config_string(s);
    }
    return h;
}

/* ---- Seed-Lock ----------------------------------------------------------
 * Damit ein laufender Run reproduzierbar bleibt (Speedrun!), frieren wir den
 * Seed beim Run-Start (Map_Setup_Menu = Neues Spiel) ein. Aenderungen am
 * Seed-Feld waehrend des Runs werden ignoriert und greifen erst beim naechsten
 * neuen Spiel. Vor dem ersten Run wird der Live-Wert genutzt.
 * ----------------------------------------------------------------------- */
static u32 sLockedSeed = 0;
static u8 sSeedLocked = 0;

/* Marathon: nach einem nahtlosen Level-Wechsel sind alle Sequence-Player auf
 * Volume 0 gefadet (Audio_FadeOutAll im GSTATE_PLAY-Zweig). Dieses Flag laesst
 * den Audio_PlaySequence-Patch die Lautstaerken beim ersten BGM des neuen
 * Levels einmalig wiederherstellen. Gesetzt im Marathon-Chain-Hook. */
static u8 sMarathonRestoreAudio = 0;

static u32 Rando_CurrentSeed(void) {
    return sSeedLocked ? sLockedSeed : Rando_ReadSeedConfig();
}
/* Eingefroren wird in Rando_OnRunStart() (Map_Setup_Menu-Hook, weiter unten). */

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
            u32 seed = Rando_CurrentSeed();
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
    /* Marathon: beim ersten BGM des neuen Levels die vom seamless-Uebergang
     * stummgeschalteten Sequence-Player wieder aufdrehen (sonst ist nach dem
     * Levelwechsel der GESAMTE Sound weg). Einmalig (One-Shot). */
    if (sMarathonRestoreAudio && (seqPlayId == SEQ_PLAYER_BGM)) {
        sMarathonRestoreAudio = 0;
        SEQCMD_SET_SEQPLAYER_VOLUME(SEQ_PLAYER_BGM, 0, 127);
        SEQCMD_SET_SEQPLAYER_VOLUME(SEQ_PLAYER_FANFARE, 0, 127);
        SEQCMD_SET_SEQPLAYER_VOLUME(SEQ_PLAYER_SFX, 0, 127);
        SEQCMD_SET_SEQPLAYER_VOLUME(SEQ_PLAYER_VOICE, 0, 127);
    }
    if ((seqPlayId == SEQ_PLAYER_BGM) && Rando_MusicEnabled()) {
        seqId = Rando_RemapBgm(seqId);
    }
    SEQCMD_SET_SEQPLAYER_IO(seqPlayId, 0, bgmParam);
    SEQCMD_PLAY_SEQUENCE(seqPlayId, fadeinTime, 0, seqId);
}

/* =========================================================================
 *  Feature: Expert Mode
 *
 *  Laesst den Run direkt im Expert-Schwierigkeitsgrad laufen, ohne dass man
 *  Expert erst im Spiel freischalten/anwaehlen muss. gExpertMode ist der globale
 *  Laufzeit-Schalter: true bedeutet doppelter Schaden (Player_ApplyDamage),
 *  aggressivere/mehr Gegner (fox_enmy.c) und das Expert-Ending.
 *
 *  Wir erzwingen ihn beim Run-Start (Map_Setup_Menu = Neues Spiel) — genau wie
 *  der Seed eingefroren wird. Damit ist die Schwierigkeit fuer den ganzen Run
 *  fix (fair fuers Racing) und greift, ohne das Savefile zu faelschen. Ist der
 *  Toggle aus, fassen wir nichts an (Vanilla: Spieler kann selbst waehlen).
 * ========================================================================= */
extern bool gExpertMode;

/* enum_expert_mode: 0 = Disabled (Default), 1 = Enabled */
static int Rando_ExpertEnabled(void) {
    return recomp_get_config_u32("enum_expert_mode") == 1;
}

/* =========================================================================
 *  Feature: Marathon Mode (continuous) — PHASE 2
 *
 *  Spielt alle Level am Stueck OHNE Karte dazwischen. Mechanik (aus Phase 1):
 *  Bei Level-Ende setzt das Spiel gNextGameState = GSTATE_MAP. Unser Hook auf
 *  Game_SetGameState erkennt die NACH-LEVEL-Transition (gLastGameState ==
 *  GSTATE_PLAY) und schreibt sie auf GSTATE_PLAY + naechstes Marathon-Level um.
 *  Der engine-eigene GSTATE_PLAY-Zweig macht den ganzen sauberen Lebenszyklus
 *  (Play_Setup -> scrubt u.a. den On-Rails-Cursor gSavedObjectLoadIndex, sonst
 *  OOB-Crash; Memory_FreeAll; PLAY_STANDBY). Karte wird nie betreten ->
 *  gMissionNumber bleibt stehen -> kein 7-Slot-Overflow. Audio: der seamless-
 *  Pfad ueberspringt den Lautstaerke-Restore der Karte -> One-Shot in unserem
 *  Audio_PlaySequence-Patch (sMarathonRestoreAudio).
 *
 *  PHASE 2: 12 normale Planeten-Level (feste ODER seed-gemischte Reihenfolge),
 *  dann der gewaehlte Gateway als letztes Marathon-Level:
 *    - Bolse  -> Venom 1 -> Venom 2 -> Andross -> Ending
 *    - Area 6 -> Venom 2 -> Andross -> Ending
 *  Das Venom-Endspiel kettet sich SELBST via GSTATE_PLAY (keine GSTATE_MAP-
 *  Transition) -> unser Hook fasst es nicht an, das Spiel laeuft allein bis zum
 *  Abspann (GSTATE_ENDING). WICHTIG: Area6/Bolse duerfen NIE im Mittel-Pool
 *  stehen (ihre Completion springt sofort zu Venom) — nur als Gateway am Ende.
 *
 *  Config:
 *    enum_marathon:       0=Disabled, 1=Bolse, 2=Area6, 3=Expert+Bolse,
 *                         4=Expert+Area6  (Expert-Varianten erzwingen gExpertMode)
 *    enum_marathon_order: 0=Shuffled (Seed, Default), 1=Fixed
 * ========================================================================= */

/* 12 normale Planeten-Level (OHNE Area6/Bolse/Venom = Gateway/Endgame). */
static const s32 kMarathonPool[] = {
    LEVEL_CORNERIA, LEVEL_METEO,   LEVEL_SECTOR_Y, LEVEL_FORTUNA,
    LEVEL_AQUAS,    LEVEL_KATINA,  LEVEL_ZONESS,   LEVEL_MACBETH,
    LEVEL_SECTOR_X, LEVEL_TITANIA, LEVEL_SECTOR_Z, LEVEL_SOLAR,
};
#define MARATHON_POOL_N ((s32) (sizeof(kMarathonPool) / sizeof(kMarathonPool[0])))
#define MARATHON_LEN (MARATHON_POOL_N + 1) /* + Gateway (Bolse/Area6) als letztes */
#define MARATHON_SALT 0x4D525448u          /* "MRTH" */

static s32 sMarathonStep = 0; /* aktueller Index; Reset bei New Game */
static u8 sMarathonPerm[MARATHON_POOL_N];
static u32 sMarathonBuiltSeed;
static u8 sMarathonPermBuilt = 0;

static u32 Rando_MarathonMode(void) {
    return recomp_get_config_u32("enum_marathon"); /* 0..4, Default 0 */
}
static int Rando_MarathonEnabled(void) {
    return Rando_MarathonMode() != 0u;
}
/* Gateway: Area 6 bei Modus 2/4, sonst Bolse (Modus 1/3). */
static int Rando_MarathonArea6(void) {
    u32 m = Rando_MarathonMode();
    return (m == 2u) || (m == 4u);
}
/* Expert-Varianten (Modus 3/4) erzwingen zusaetzlich Expert Mode. */
static int Rando_MarathonExpert(void) {
    u32 m = Rando_MarathonMode();
    return (m == 3u) || (m == 4u);
}
/* enum_marathon_order: 0 = Shuffled (Seed, Default), 1 = Fixed. */
static int Rando_MarathonFixedOrder(void) {
    return recomp_get_config_u32("enum_marathon_order") == 1u;
}

static void Rando_BuildMarathonPerm(u32 seed) {
    s32 i;
    for (i = 0; i < MARATHON_POOL_N; i++) {
        sMarathonPerm[i] = (u8) i;
    }
    Rando_RngSeed(seed ^ MARATHON_SALT);
    for (i = MARATHON_POOL_N - 1; i > 0; i--) {
        u32 j = Rando_RngNext() % (u32) (i + 1);
        u8 tmp = sMarathonPerm[i];
        sMarathonPerm[i] = sMarathonPerm[j];
        sMarathonPerm[j] = tmp;
    }
    sMarathonBuiltSeed = seed;
    sMarathonPermBuilt = 1;
}

/* Welches LEVEL gehoert zu Marathon-Schritt 'step'?
 *   step 0 .. POOL_N-1 = Mittel-Pool (fest oder seed-gemischt)
 *   step >= POOL_N      = Gateway (Bolse/Area6) -> danach Venom-Selbstkette */
static s32 Rando_MarathonLevel(s32 step) {
    if (step >= MARATHON_POOL_N) {
        return Rando_MarathonArea6() ? LEVEL_AREA_6 : LEVEL_BOLSE;
    }
    if (step < 0) {
        step = 0;
    }
    if (Rando_MarathonFixedOrder()) {
        return kMarathonPool[step];
    }
    {
        u32 seed = Rando_CurrentSeed();
        if (!sMarathonPermBuilt || (seed != sMarathonBuiltSeed)) {
            Rando_BuildMarathonPerm(seed);
        }
        return kMarathonPool[sMarathonPerm[step]];
    }
}

/*
 * Naechstes Level verketten: laeuft am Anfang von Game_SetGameState, BEVOR
 * dessen switch gNextGameState liest. Nur echte Nach-Level-Transitionen zur
 * Karte werden umgelenkt (gLastGameState == GSTATE_PLAY) — die erste Karte
 * beim Neuen Spiel (aus dem Menue) bleibt unangetastet.
 */
RECOMP_HOOK("Game_SetGameState") void Rando_MarathonChain(void) {
    s32 i;
    if (!Rando_MarathonEnabled()) {
        return;
    }
    if ((gNextGameState != GSTATE_MAP) || (gLastGameState != GSTATE_PLAY)) {
        return; /* keine Nach-Level Karten-Transition */
    }
    if ((sMarathonStep + 1) >= MARATHON_LEN) {
        return; /* Kette zu Ende -> regulaer zur Karte/Ending */
    }
    sMarathonStep++;

    /* CRASH-SCHUTZ (noetig): haengende SFX-Referenz auf den gleich
     * freigegebenen Spieler entfernen — sonst dereferenziert der Audio-Code nach
     * Memory_FreeAll den alten gPlayer -> Crash. Reines SFX (kein BGM). */
    Audio_StopPlayerNoise(0);
    Audio_KillSfxBySource(gPlayer[0].sfxSource);
    Play_ClearObjectData();
    gSavedGoldRingCount[0] = gGoldRingCount[0];
    for (i = TEAM_ID_FALCO; i < TEAM_ID_MAX; i++) {
        gSavedTeamShields[i] = gTeamShields[i];
    }

    /* DER eigentliche Crash-Fix: den On-Rails-Streaming-Cursor nullen, damit
     * func_enmy_80062568() im naechsten Player_Setup nicht out-of-bounds liest.
     * Play_Setup() (laeuft gleich im GSTATE_PLAY-Zweig) nullt das ebenfalls,
     * wir setzen es vorab als Sicherheitsgurt. */
    gSavedObjectLoadIndex = 0;
    gObjectLoadIndex = 0;
    gSavedPathProgress = 0.0f;

    /* Transition von GSTATE_MAP auf GSTATE_PLAY umschreiben und den ORIGINAL-
     * Body von Game_SetGameState die Arbeit machen lassen: gCurrentLevel =
     * gNextLevel; Play_Setup() (scrubt gCsWasNotSkipped/Cursor/Phase/Timer);
     * gPlayState = PLAY_STANDBY; Memory_FreeAll(); gNextGameStateTimer = 3.
     * KEIN gPlayState/gLoadLevelObjects/gCurrentLevel hier selbst setzen. */
    gNextLevel = (u16) Rando_MarathonLevel(sMarathonStep);
    gNextLevelPhase = 0;
    gNextGameState = GSTATE_PLAY;

    /* Der GSTATE_PLAY-Zweig fadet gleich alle Sequence-Player auf 0
     * (Audio_FadeOutAll). Da wir die Karte ueberspringen (die das normal wieder
     * aufdreht), Lautstaerke-Restore beim ersten BGM des neuen Levels anstossen. */
    sMarathonRestoreAudio = 1;
}

/* =========================================================================
 *  Feature: Random Planets (Herzstueck) — Routen-Version
 *
 *  Wir randomisieren auf PLANETEN-Ebene, nicht erst beim Level-Start. So
 *  bleiben Map-Anzeige, gespieltes Level UND Score-Screen synchron, denn alle
 *  drei leiten sich aus EINER Variable ab: sNextPlanetId.
 *  (in Map: sCurrentPlanetId = sNextPlanetId; gMissionPlanet[n] = ...;
 *           Map_CurrentLevel_Setup() -> gCurrentLevel)
 *
 *  Route (feste Laenge 7, wie der Score-Screen): Slot 0 = Corneria (Vanilla-
 *  Start), Slots 1..5 = geseedete, eindeutige Zufallsplaneten, Slot 6 = Venom.
 *  Das fixt zugleich das "ein Level zu wenig"-Gefuehl: immer 7 Stops.
 *
 *  Topologie-Hinweis: Die Map kennt nur 24 feste Pfade. Bei nicht benachbarten
 *  Planeten gibt Map_GetPathId() sonst 24 zurueck -> sPaths[24] = Out-of-Bounds.
 *  Darum patchen wir Map_GetPathId so, dass es 0 statt 24 liefert (kein OOB;
 *  nur die verbindende Pfad-Linie sieht ggf. kosmetisch anders aus).
 * ========================================================================= */
extern PlanetPath sPaths[24];
extern PlanetId sNextPlanetId;

/* Mittel-Planeten-Pool: OHNE Corneria (Start), Venom (Endgame) UND ohne
 * Area 6 / Bolse — das sind die "Proxy"-Planeten, die direkt zu Venom fuehren
 * (Area6 -> Venom2, Bolse -> Venom1). Die gehoeren ans Routen-Ende, nicht in
 * die Mitte, sonst kollabiert die Route sofort zu Venom. (Genau wie im
 * Original-Randomizer.) */
static const PlanetId kPlanetPool[] = {
    PLANET_METEO,    PLANET_SECTOR_Z, PLANET_SECTOR_X, PLANET_SECTOR_Y,
    PLANET_KATINA,   PLANET_MACBETH,  PLANET_ZONESS,   PLANET_TITANIA,
    PLANET_AQUAS,    PLANET_FORTUNA,  PLANET_SOLAR,
};
#define PLANET_POOL_N ((u32) (sizeof(kPlanetPool) / sizeof(kPlanetPool[0])))
#define PLANETS_SALT 0x504C4E31u /* "PLN1" */
#define ROUTE_PROXY_SLOT 5       /* Area6/Bolse (Venom-Gateway) */
#define ROUTE_VENOM_SLOT 6       /* Venom-Finale */

static u8 sPlanetPerm[PLANET_POOL_N];
static u32 sPlanetBuiltSeed;
static u8 sPlanetPermBuilt = 0;
static u8 sProxyIsArea6 = 0; /* seed-bestimmt: Area 6 (1) oder Bolse (0) */

static void Rando_BuildPlanetPerm(u32 seed) {
    u32 i;
    for (i = 0; i < PLANET_POOL_N; i++) {
        sPlanetPerm[i] = (u8) i;
    }
    Rando_RngSeed(seed ^ PLANETS_SALT);
    for (i = PLANET_POOL_N - 1u; i > 0u; i--) {
        u32 j = Rando_RngNext() % (i + 1u);
        u8 tmp = sPlanetPerm[i];
        sPlanetPerm[i] = sPlanetPerm[j];
        sPlanetPerm[j] = tmp;
    }
    sProxyIsArea6 = (u8) (Rando_RngNext() & 1u); /* Area6 oder Bolse */
    sPlanetBuiltSeed = seed;
    sPlanetPermBuilt = 1;
}

static int Rando_PlanetsEnabled(void) {
    return recomp_get_config_u32("enum_random_planets") == 0;
}

/* enum_start_planet: 0 = Random (wie Original, Default), 1 = immer Corneria */
static int Rando_CorneriaStart(void) {
    return recomp_get_config_u32("enum_start_planet") == 1;
}

/* Welcher Planet gehoert zu Run-Slot 'slot'?
 *   Slot 0      = Corneria (bei "Corneria Start"), sonst geseedet (perm[0])
 *   Slot 1..4   = geseedete Zufallsplaneten (perm[slot])
 *   Slot 5      = Area 6 / Bolse (Venom-Gateway, seed-bestimmt)
 *   Slot >= 6   = Venom (Finale)
 * Slots 1..4 bleiben seed-identisch, egal welche Start-Option (kein Versatz). */
static PlanetId Rando_RoutePlanet(s32 slot) {
    u32 seed = Rando_CurrentSeed();
    if (!sPlanetPermBuilt || (seed != sPlanetBuiltSeed)) {
        Rando_BuildPlanetPerm(seed);
    }
    if ((slot == 0) && Rando_CorneriaStart()) {
        return PLANET_CORNERIA;
    }
    if (slot >= ROUTE_VENOM_SLOT) {
        return PLANET_VENOM;
    }
    if (slot == ROUTE_PROXY_SLOT) {
        return sProxyIsArea6 ? PLANET_AREA_6 : PLANET_BOLSE;
    }
    return kPlanetPool[sPlanetPerm[(u32) slot % PLANET_POOL_N]];
}

/* OOB-Schutz: Original gibt bei nicht gefundenem Pfad 24 zurueck (sPaths[24]!). */
RECOMP_PATCH s32 Map_GetPathId(PlanetId start, PlanetId end) {
    s32 i;
    for (i = 0; i < 24; i++) {
        if ((sPaths[i].start == start) && (sPaths[i].end == end)) {
            return i;
        }
    }
    return 0;
}

/* Map-Symbole fuers Erzwingen der Route. */
extern PlanetId sCurrentPlanetId;
extern PlanetId gMissionPlanet[];
void Map_CurrentLevel_Setup(void);

/*
 * Route ERZWINGEN am einzigen Lade-Punkt (Map_PlayLevel, Entry-Hook).
 *
 * Wichtig: Der Spieler waehlt auf der Map selbst den Pfad (MAP_PATH_CHANGE
 * entlang der Graph-Nachbarn). Ein frueher sNextPlanetId-Override wird davon
 * wieder ueberschrieben. Darum setzen wir den Planeten erst HIER, direkt bevor
 * Play_Setup() gCurrentLevel liest -> egal was navigiert wurde, gespielt wird
 * unsere Route. sCurrentPlanetId + gMissionPlanet[] werden mitgezogen, damit
 * Score-Screen/Anzeige stimmen. Map-Navigation wird damit rein kosmetisch.
 *
 * Deckt ALLE Slots ab (auch Slot 0 = erstes Level). gMissionNumber = Run-Slot.
 */
RECOMP_HOOK("Map_PlayLevel") void Rando_ForceLevel(void) {
    PlanetId p;
    if (Rando_MarathonEnabled()) {
        /* Marathon steuert die Kette: erstes Level direkt setzen (alle
         * weiteren laufen ueber den Game_SetGameState-Hook, nicht hierher).
         * Hat Vorrang vor Random Planets. */
        gCurrentLevel = (LevelId) Rando_MarathonLevel(sMarathonStep);
        return;
    }
    if (!Rando_PlanetsEnabled()) {
        return;
    }
    p = Rando_RoutePlanet(gMissionNumber);
    sCurrentPlanetId = p;
    if ((gMissionNumber >= 0) && (gMissionNumber < 7)) {
        gMissionPlanet[gMissionNumber] = p;
    }
    Map_CurrentLevel_Setup(); /* gCurrentLevel aus sCurrentPlanetId ableiten */
}

/* Run-Start (Map_Setup_Menu = Neues Spiel): Seed einfrieren (Reproduzierbar)
 * und Expert-Mode fuer den Run setzen, falls der Toggle an ist. */
RECOMP_HOOK_RETURN("Map_Setup_Menu") void Rando_OnRunStart(void) {
    sLockedSeed = Rando_ReadSeedConfig();
    sSeedLocked = 1;
    sMarathonStep = 0; /* Marathon-Kette von vorne */
    if (Rando_MarathonEnabled() && !Rando_MarathonFixedOrder()) {
        Rando_BuildMarathonPerm(sLockedSeed); /* gemischte Reihenfolge fuer den Run einfrieren */
    }
    if (Rando_ExpertEnabled() || Rando_MarathonExpert()) {
        gExpertMode = true; /* fuer den ganzen Run fix, wie der Seed */
    }
}

/* =========================================================================
 *  Feature: Random Items
 *
 *  Beim Item-Spawn (Item_Load) tauschen wir den Item-Typ gegen einen anderen
 *  aus einem festen Pool — als geseedete Permutation (Typ -> Typ). Damit ist
 *  es voll deterministisch: bei gleichem Seed wird z.B. jede Bombe immer zum
 *  selben anderen Item. Speedrun-tauglich & lernbar.
 *
 *  WICHTIG: Nur echte Pickups im Pool. OBJ_ITEM_PATH_* sind Pfad-Marker
 *  (steuern Level-Routen) und CHECKPOINT/METEO_WARP/RING_CHECK sind Spezial-
 *  Objekte — die fassen wir NICHT an.
 * ========================================================================= */
static const u16 kItemPool[] = {
    OBJ_ITEM_LASERS,   OBJ_ITEM_SILVER_RING, OBJ_ITEM_SILVER_STAR, OBJ_ITEM_BOMB,
    OBJ_ITEM_1UP,      OBJ_ITEM_GOLD_RING,   OBJ_ITEM_WING_REPAIR,
};
#define ITEM_POOL_N ((u32) (sizeof(kItemPool) / sizeof(kItemPool[0])))
#define ITEMS_SALT 0x49544D31u /* "ITM1" */

static u8 sItemPerm[ITEM_POOL_N];
static u32 sItemBuiltSeed;
static u8 sItemPermBuilt = 0;

static void Rando_BuildItemPerm(u32 seed) {
    u32 i;
    for (i = 0; i < ITEM_POOL_N; i++) {
        sItemPerm[i] = (u8) i;
    }
    Rando_RngSeed(seed ^ ITEMS_SALT);
    for (i = ITEM_POOL_N - 1u; i > 0u; i--) {
        u32 j = Rando_RngNext() % (i + 1u);
        u8 tmp = sItemPerm[i];
        sItemPerm[i] = sItemPerm[j];
        sItemPerm[j] = tmp;
    }
    sItemBuiltSeed = seed;
    sItemPermBuilt = 1;
}

static int Rando_ItemsEnabled(void) {
    return recomp_get_config_u32("enum_random_items") == 0;
}

static u32 Rando_RemapItem(u32 id) {
    u32 i;
    if (!Rando_ItemsEnabled()) {
        return id;
    }
    for (i = 0; i < ITEM_POOL_N; i++) {
        if (kItemPool[i] == id) {
            u32 seed = Rando_CurrentSeed();
            if (!sItemPermBuilt || (seed != sItemBuiltSeed)) {
                Rando_BuildItemPerm(seed);
            }
            return kItemPool[sItemPerm[i]];
        }
    }
    return id; /* kein Pool-Item (z.B. Pfad-Marker) -> unveraendert */
}

/* Original Item_Load (fox_enmy.c, 13 Zeilen) — nur die ID-Zuweisung remappt. */
RECOMP_PATCH void Item_Load(Item* this, ObjectInit* objInit) {
    Item_Initialize(this);
    this->obj.status = OBJ_INIT;
    this->obj.pos.z = -objInit->zPos1;
    this->obj.pos.z += -3000.0f + objInit->zPos2;
    this->obj.pos.x = objInit->xPos;
    this->obj.pos.y = objInit->yPos;
    this->obj.rot.y = objInit->rot.y;
    this->obj.rot.x = objInit->rot.x;
    this->obj.rot.z = objInit->rot.z;
    this->obj.id = Rando_RemapItem(objInit->id);
    this->width = 1.0f;
    Object_SetInfo(&this->info, this->obj.id);
}

/* =========================================================================
 *  Feature: One-Hit-KO (Hardmode)
 *
 *  Fox stirbt bei JEDEM Treffer sofort. Das ASM-Original patcht dafuer einen
 *  Store-Befehl so um, dass 0 in die Gesundheit geschrieben wird. Im Decomp
 *  setzen wir einfach den Schildbalken (player->shields) auf 0 = Tod.
 *
 *  WICHTIG (Crash-Fix): Das muss ein ENTRY-Hook (RECOMP_HOOK) sein, KEIN
 *  Return-Hook. Bei RECOMP_HOOK_RETURN sind die Argument-Register (a0=player)
 *  nach dem Funktionsdurchlauf evtl. ueberschrieben -> 'player' war Muell ->
 *  player->shields = 0 schrieb an eine wilde Adresse -> harter Crash zum
 *  Desktop. Beim Entry-Hook sind die Argumente garantiert gueltig.
 *
 *  Da wir am Funktionsanfang sind, pruefen wir die Schild-Item-Absorption
 *  selbst (statt auf das spaeter gesetzte player->damage zu schauen):
 *  - damage == 0  -> kein echter Treffer, nichts tun.
 *  - gHasShield[num] -> das Schild-Item absorbiert diesen Treffer, nicht toeten.
 *  Sonst: shields = 0. Rein deterministisch, kein Seed-Bezug.
 * ========================================================================= */
static int Rando_OneHitKOEnabled(void) {
    return recomp_get_config_u32("enum_one_hit_ko") == 1; /* Default: Disabled */
}

RECOMP_HOOK("Player_ApplyDamage")
void Rando_OneHitKO(Player* player, s32 direction, s32 damage) {
    (void) direction;
    if (!Rando_OneHitKOEnabled()) {
        return;
    }
    if (damage == 0) {
        return; /* kein echter Treffer */
    }
    if (gHasShield[player->num]) {
        return; /* aktives Schild-Item absorbiert diesen Treffer */
    }
    player->shields = 0; /* sofortiger Tod */
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
