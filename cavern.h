// ============================================================================
//  cavern.h - everything the parts of Cavern Runner share
//
//  The program is split into files by job:
//      main.cpp      the window, the game loop, keyboard and mouse
//      game.cpp      the rules: moving the player, monsters, boss, hazards
//      draw.cpp      drawing the game, the status strip and messages
//      editor.cpp    the cavern editor
//      designer.cpp  the pixel editor for characters and tiles
//      files.cpp     reading and writing caverns.txt and sprites.txt
//      scores.cpp    the high-score table
//      sound.cpp     the sound mixer, effects and music, and sounds.txt
//      audio.cpp     the sound designer
//      checker.cpp   the cavern checker: can every key be reached?
//      demo.cpp      the demo: the game playing itself on the title screen
//      pad.cpp       gamepads
//      inputbox.cpp  a small "type some text" window
//      defaults.cpp  the standard caverns and artwork
//
//  Each .cpp file includes this header, so they all agree on the sizes,
//  the types, and the names of the shared variables and functions.
//  "extern" means "this exists, but is defined in one of the .cpp files".
// ============================================================================

#ifndef CAVERN_H
#define CAVERN_H

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

// ---- Sizes (all in pixels) -------------------------------------------------
#define TILE        32
#define MAP_W       96                  // level width in tiles
#define MAP_H       20                  // level height in tiles
#define VIEW_W      (30 * TILE)         // visible part of the level: 30 tiles wide
#define VIEW_H      (MAP_H * TILE)
#define HUD_H       96                  // status strip under the play area
#define GAME_W      VIEW_W              // the picture we draw; the window scales it
#define GAME_H      (VIEW_H + HUD_H)

#define PLAYER_W    20
#define PLAYER_H    28
#define ENEMY_W     24
#define ENEMY_H     28

// ---- Physics (per frame, at 60 frames a second) ----------------------------
#define WALK_SPEED      3
#define JUMP_SPEED      11.0f           // upward speed when a jump starts
#define GRAVITY         0.55f           // added to the vertical speed each frame
#define MAX_FALL        10.0f
#define BELT_SPEED      1               // how hard a conveyor pushes you
#define CLIMB_SPEED     2.0f            // up and down ropes
#define MOVER_SPEED     1.0f            // moving platforms

#define AIR_MAX         (60 * 90)       // 90 seconds of air
#define CRUMBLE_TIME    40              // frames a crumbling tile lasts underfoot
#define MAX_ENEMIES     24
#define MAX_MOVERS      16
#define START_LIVES     3
#define MAX_LIVES       9
#define EXTRA_LIFE      10000           // an extra life every this many points
#define MAX_CAVERNS     50

#define CAVERN_FILE     "caverns.txt"
#define SPRITE_FILE     "sprites.txt"
#define SCORE_FILE      "scores.txt"
#define SOUND_FILE      "sounds.txt"

//  Map characters:
//  #  wall           =  platform        -  crumbling platform   <  >  conveyors
//  ^  spikes         *  poison plant    s  switch               D  door (a switch opens it)
//  |  rope           ~  moving platform k  key
//  E F G  monsters   P  player start    X  exit
#define TILE_CHARS      " #=-<>^*sD|~kEFGPX"

// ============================================================================
//  Artwork: sprites are 16 x 16 pixels (the boss is 32 x 32), drawn at
//  double size, in 16 colours.
// ============================================================================

#define SPR             16
#define BOSS_SPR        32
#define SPR_MAX         32
#define SPR_SCALE       2
#define SPR_SIZE        (SPR * SPR_SCALE)   // a normal sprite on screen: 32 x 32
#define NUM_MONSTERS    16
#define NUM_TILESETS    4

// The tiles in each tile set
enum
{
    T_WALL, T_PLATFORM, T_CRUMBLE, T_CONVEYOR, T_SPIKES, T_PLANT,
    T_SWITCH_OFF, T_SWITCH_ON, T_DOOR, T_ROPE, T_MOVER,
    TILES_PER_SET
};

enum
{
    SPR_PLAYER1, SPR_PLAYER2, SPR_KEY,
    SPR_MONSTER,                                    // monster m, frame f: SPR_MONSTER + m * 2 + f
    SPR_BOSS1 = SPR_MONSTER + NUM_MONSTERS * 2,
    SPR_BOSS2,
    SPR_TILES,                                      // tile t of set s: TileSprite(s, t)
    NUM_SPRITES = SPR_TILES + NUM_TILESETS * TILES_PER_SET
};

typedef struct
{
    signed char px[SPR_MAX][SPR_MAX];   // -1 = transparent, otherwise a colour 0-15
} Sprite;                               // (a 16 x 16 sprite uses the top-left corner)

extern Sprite         sprites[NUM_SPRITES];
extern char           spriteIds[NUM_SPRITES][16];   // names in sprites.txt
extern const COLORREF palette[16];
extern const char    *colourNames[16];

// The things the designer lets you draw, in tabs: characters, then each tile set
typedef struct
{
    char name[24];
    int  first;                         // its first sprite
    int  frames;                        // 1 or 2
    int  tab;                           // 0 = characters, 1-4 = tile sets
} DesignItem;

enum { CHAR_PLAYER, CHAR_KEY, CHAR_MONSTER1, CHAR_BOSS = CHAR_MONSTER1 + NUM_MONSTERS, NUM_CHARS };
#define TILE_ITEMS  10                  // per tile set (the switch's two frames are one item)
#define NUM_ITEMS   (NUM_CHARS + NUM_TILESETS * TILE_ITEMS)
#define NUM_TABS    (1 + NUM_TILESETS)

extern DesignItem items[NUM_ITEMS];

// ============================================================================
//  Caverns
// ============================================================================

#define MONSTER_KINDS 3                 // E, F and G

enum { MOVE_WALK, MOVE_RUN, MOVE_LIFT, MOVE_FLY, MOVE_BOUNCE, NUM_MOVES };
extern const char *moveNames[NUM_MOVES];

#define NUM_SCHEMES 6                   // background colour schemes

typedef struct
{
    char     name[64];
    COLORREF sky, skyLines;             // background and its parallax stripes
    int      scheme;                    // which preset background it started from
    int      tileset;                   // 0-3
    int      music;                     // which tune, 0-2
    int      monster[MONSTER_KINDS];    // the monster designs (0-15) for E, F and G
    int      move[MONSTER_KINDS];       // and how each kind moves
    BOOL     boss;                      // can the boss turn up here?
    char     rows[MAP_H][MAP_W + 1];    // the map, one string per row
} Cavern;

extern Cavern caverns[MAX_CAVERNS];
extern int    numCaverns;
extern const COLORREF schemes[NUM_SCHEMES][2];

// ============================================================================
//  Game state
// ============================================================================

enum { ST_TITLE, ST_PLAYING, ST_DYING, ST_GAMEOVER, ST_TALLY, ST_CLEARED, ST_VICTORY,
       ST_EDIT, ST_SPRITES, ST_SOUNDS };

typedef struct
{
    float x, y;             // top-left corner in the level
    float vx, vy;           // speed
    int   kind;             // 0, 1 or 2 for E, F or G
} Enemy;

typedef struct
{
    float x;                // left edge
    int   y;                // top edge (it only moves sideways)
    int   w;                // width in pixels
    float vx;
    float dx;               // how far it moved this frame, to carry the player
} Mover;

enum { BOSS_NONE, BOSS_WAITING, BOSS_WARNING, BOSS_HUNTING, BOSS_LEAVING };
#define BOSS_W      (BOSS_SPR * SPR_SCALE)  // 64 x 64 on screen
#define BOSS_HIT    40                      // its collision box, centred
#define BOSS_SPEED  0.9f                    // pixels per frame: you can outrun it
#define BOSS_STAY   (60 * 20)               // it hunts for 20 seconds, then leaves
#define BOSS_BONUS  500                     // points for surviving it

extern HWND   hwndMain;
extern Cavern *cav;                     // the cavern being played or edited
extern int    cavNum;
extern char   map[MAP_H][MAP_W + 1];    // working copy of the level while playing
extern unsigned char crumble[MAP_H][MAP_W];

extern int    px;                       // player position (top-left)
extern float  py, vy;
extern BOOL   onGround, climbing;
extern int    airDir, facing, walkAnim;

extern Enemy  enemies[MAX_ENEMIES];
extern int    numEnemies;
extern Mover  movers[MAX_MOVERS];
extern int    numMovers;

extern int    exitX, exitY, keysLeft, keysTotal, air;
extern int    score, lives;
extern BOOL   switchOn;
extern int    state, stateTimer, frameCount;
extern float  camX;
extern BOOL   paused, active, testMode;
extern int    bossState;
extern float  bossX, bossY, bossVX;
extern int    savedTimer;
extern int    titlePage;                // 0 = instructions, 1 = high scores

extern char     message[64];            // a big message shown for a while
extern COLORREF messageColour;
extern int      messageTimer;

// ---- game.cpp
// Everything about the player that the physics uses, so the cavern checker
// can try a move, look at the result, and put things back
typedef struct
{
    int   px;
    float py, vy;
    BOOL  onGround, climbing;
    int   airDir, facing, grabDelay, standingOn;
} PlayerState;

extern const unsigned char *simKeys;    // when set, KeyDown reads these instead of the keyboard
extern BOOL simulating;                 // the checker is running: no sounds
void  GetPlayerState(PlayerState *s);
void  SnapshotSave(void);               // for looking ahead: save the game...
void  SnapshotRestore(void);            // ...and put it back afterwards
void  SimulateFrame(void);
void  SetPlayerState(const PlayerState *s);
void  UpdatePlayer(void);
void  SetState(int s);
void  LoadLevel(int n);
void  ShowTitle(void);
void  NewGame(void);
void  EndGame(void);                    // offers the high-score table, then the title
void  Update(void);
char  TileAt(int tx, int ty);
BOOL  IsWall(char c);
BOOL  IsFloor(char c);
BOOL  KeyDown(int vk);
float MaxCamera(void);
void  ShowMessage(const char *text, COLORREF colour, int frames);

// ---- draw.cpp
extern HFONT hFontSmall, hFontHud, hFontBig, hFontTitle;
void MakeFonts(void);
void FreeFonts(void);
void Render(HDC dc);
void Box(HDC dc, int x, int y, int w, int h, COLORREF c);
void Frame(HDC dc, int x, int y, int w, int h, int thick, COLORREF c);
void DrawSprite(HDC dc, int n, int x, int y, int scale, BOOL mirror);
void DrawSpriteEx(HDC dc, int n, int x, int y, int scale, BOOL mirror, int shift, int rows);
void DrawTile(HDC dc, char c, int tx, int ty, int sx, int sy);
void DrawExit(HDC dc, int sx, int sy, BOOL open);
void DrawEnemy(HDC dc, int sx, int sy, float vx, int design);
void DrawPlayer(HDC dc, int sx, int sy);
void TextAt(HDC dc, HFONT font, int x, int y, const char *s, COLORREF col);
void CenterText(HDC dc, HFONT font, const char *s, int y, COLORREF col);
int  TileSprite(int set, int t);

// ---- editor.cpp
extern BOOL editModified;
void EnterEditor(int n);
void EditKey(WPARAM key);
void EditMouseDown(int x, int y, BOOL right);
void EditMouseMove(int x, int y, WPARAM buttons);
void EditWheel(int delta);
void DrawEditor(HDC dc);
BOOL EditCanClose(void);

// ---- checker.cpp
extern BOOL checkDone;                  // results to show in the editor
extern unsigned char checkMark[MAP_H][MAP_W];   // 1 = can stand here, 2 = can't get back from here
extern char checkSummary[120];
extern BOOL checkAllGood;
BOOL CheckMissed(int tx, int ty);       // a key, switch or exit that can't be reached?
void CheckCavern(void);

// One move of a planned route: what to press, and where it should end up
typedef struct
{
    int         move;                   // 0-4, as in the checker
    BOOL        rope;                   // starting from a rope?
    PlayerState after;
} DemoStep;
int  PlanRoute(DemoStep *steps, int max);

// ---- demo.cpp
extern BOOL demoMode;
BOOL StartDemo(void);
void StopDemo(void);
void DemoInput(void);                   // before the player moves
void DemoAfter(void);                   // after

// ---- pad.cpp
void PadPoll(void);                     // call once a frame
BOOL PadHeld(int vk);                   // is the pad doing what this key does?
BOOL PadAnyPressed(void);               // any button just pressed?
int  PadPressed(void);                  // a key for a button just pressed, or 0

// ---- designer.cpp
extern BOOL sprModified;
void EnterDesigner(void);
void DesignKey(WPARAM key);
void DesignMouseDown(int x, int y, BOOL right);
void DesignMouseMove(int x, int y, WPARAM buttons);
void DrawDesigner(HDC dc);
BOOL DesignCanClose(void);

// ---- files.cpp
void  InitSpriteTables(void);
int   SpriteSize(int n);
BOOL  SpriteBlank(int n);
void  ExePath(const char *fileName, char *path);
char *ReadTextFile(const char *path);
BOOL  WriteTextFile(const char *path, const char *text);
BOOL  StartsWith(const char *line, const char *word);
const char *NextLine(const char *text, char *line, int size);
void  SetScheme(Cavern *cv, int s);
void  ClearCavern(Cavern *cv, const char *name, int index);
void  LoadCaverns(void);
BOOL  SaveCaverns(void);
int   ImportDefaultCaverns(void);
void  LoadSprites(void);
BOOL  SaveSprites(void);
const char *CavernProblem(int i);
int   NextPlayable(int from);
int   CountProblems(void);
BOOL  CavernHasTile(const Cavern *cv, char c);

// ---- scores.cpp
#define NUM_SCORES 10
typedef struct
{
    int  score;
    char name[16];
} ScoreEntry;

extern ScoreEntry scores[NUM_SCORES];
void LoadScores(void);
int  HiScore(void);
BOOL ScoreQualifies(int s);
void AddScore(int s, const char *name);

// ---- sound.cpp
enum { SND_JUMP, SND_KEY, SND_OPEN, SND_DIE, SND_CLEARED, SND_START, SND_GAMEOVER,
       SND_BOSSWARN, SND_BOSSGONE, SND_SWITCH, SND_EXTRA, NUM_SOUNDS };

// An effect is a list of notes, each sliding from one pitch to another
#define MAX_FX_NOTES 16
typedef struct
{
    int startHz, endHz;                 // 0 = silence
    int ms;                             // length in milliseconds
} SoundNote;

typedef struct
{
    int       count;
    SoundNote notes[MAX_FX_NOTES];
} EffectDef;

// The tunes: MIDI note numbers (60 = middle C) and lengths in steps.
// Each cavern chooses one.
#define MAX_TUNE  256
#define NUM_TUNES 3
typedef struct
{
    unsigned char note, len;
} TuneNote;

typedef struct
{
    TuneNote notes[MAX_TUNE];
    int      len;
    int      tempo;                     // milliseconds per step
} Tune;

extern EffectDef   effectDefs[NUM_SOUNDS];
extern const char *soundNames[NUM_SOUNDS];
extern Tune        tunes[NUM_TUNES];
extern const char *tuneNames[NUM_TUNES];
extern BOOL        previewing;          // the sound designer is playing a tune
extern int         previewTune;         // which one
extern int         previewNote;         // and which note

extern BOOL soundOn, musicOn;
void SoundInit(void);
void RebuildEffect(int n);              // after changing effectDefs[n]
void PreviewTune(int t, int from);
void StopPreview(void);
void LoadSounds(void);
BOOL SaveSounds(void);
void StandardSound(int which);          // NUM_SOUNDS + t means tune t

// ---- audio.cpp (the sound designer)
extern BOOL audioModified;
void EnterAudio(void);
void AudioKey(WPARAM key);
void AudioMouseDown(int x, int y);
void DrawAudio(HDC dc);
BOOL AudioCanClose(void);
void SoundShutdown(void);
void SoundPump(void);                   // call often: keeps the sound card fed
void Play(int effect);
void StopSounds(void);

// ---- inputbox.cpp
BOOL InputBox(HWND owner, const char *title, const char *prompt, char *buf, int size);

// ---- defaults.cpp
extern const char defaultCaverns[];
extern const char defaultSprites[];
extern const char defaultSounds[];

#endif
