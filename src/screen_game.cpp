#include "screen_game.h"
#include "config.h"
#include "globals.h"
#include "buzzer.h"
#include "storage.h"
#include <math.h>

// ── Game State ──────────────────────────────────────────────────
GameState gameState = GAME_READY;

float  gamePlayerY  = GAME_GROUND_Y - GAME_PLAYER_SZ * 2;
float  gameVelY     = 0;
bool   gameOnGround = true;
bool   gameCanDouble = false;
float  gamePlayerRot = 0;
static float  gameSquash    = 1.0f;
static unsigned long gameLandMs = 0;

// Obstacle types:
// 0=single spike, 1=double spike, 2=tall spike
// 3=platform (landable block), 4=ramp (auto-launch), 5=saw blade (spinning)
// 6=platform+spike (platform with spike on top)
#define GAME_MAX_OBS     8
#define GAME_MAX_PARTS  20
#define GAME_MAX_STARS  18
#define GAME_MAX_COLS    6

struct Obstacle {
    float x;
    int   halfW, h, type;
    bool  active, scored;
    float rot;  // for saw blade spin
};
static Obstacle gameObs[GAME_MAX_OBS];

struct Particle {
    float x, y, vx, vy;
    uint16_t col;
    int life;
    bool active;
};
static Particle gameParts[GAME_MAX_PARTS];

struct Star {
    float x, y;
    uint16_t col;
    float speed;
};
static Star gameStars[GAME_MAX_STARS];

struct BgColumn {
    float x;
    int   w, h;
    uint16_t col;
    float speed;
};
static BgColumn gameBgCols[GAME_MAX_COLS];

float  gameSpeed     = 2.8f;
int    gameScore     = 0;
int    gameHiScore   = 0;
unsigned long gameFrameMs = 0;
static float  gameSpawnDist = 0;
static int    gameFlashTimer = 0;
static uint16_t gameFlashCol = COL_WHITE;

uint16_t gameGroundCol  = COL_DARK_GRAY;
uint16_t gamePlayerCol  = COL_GREEN;
uint16_t gameAccentCol  = COL_CYAN;
static uint16_t gameBgCol = 0x0000;
static float gameBgScroll = 0;

// ── Helpers ─────────────────────────────────────────────────────
static void gameUpdateTheme() {
    int tier = gameScore / 10;
    switch (tier % 6) {
        case 0: gamePlayerCol = COL_GREEN;   gameAccentCol = COL_CYAN;       gameGroundCol = COL_DARK_GRAY; gameBgCol = 0x0000; break;
        case 1: gamePlayerCol = COL_CYAN;    gameAccentCol = COL_MAGENTA;    gameGroundCol = 0x2104; gameBgCol = 0x0821; break;
        case 2: gamePlayerCol = COL_YELLOW;  gameAccentCol = COL_ORANGE;     gameGroundCol = 0x1082; gameBgCol = 0x1000; break;
        case 3: gamePlayerCol = COL_MAGENTA; gameAccentCol = COL_LIGHT_BLUE; gameGroundCol = 0x0811; gameBgCol = 0x0811; break;
        case 4: gamePlayerCol = COL_ORANGE;  gameAccentCol = COL_GREEN;      gameGroundCol = 0x2945; gameBgCol = 0x0841; break;
        case 5: gamePlayerCol = COL_GOLD;    gameAccentCol = COL_RED;        gameGroundCol = 0x18C3; gameBgCol = 0x1020; break;
    }
}

void gameSpawnParticle(float x, float y, float vx, float vy, uint16_t col, int life) {
    for (int i = 0; i < GAME_MAX_PARTS; i++) {
        if (!gameParts[i].active) {
            gameParts[i] = {x, y, vx, vy, col, life, true};
            return;
        }
    }
}

static void gameInitStars() {
    for (int i = 0; i < GAME_MAX_STARS; i++) {
        gameStars[i].x = random(240);
        gameStars[i].y = 20 + random(GAME_GROUND_Y - 40);
        uint8_t b = 4 + random(12);
        gameStars[i].col = (b << 11) | (b << 6) | b;
        gameStars[i].speed = 0.15f + (random(100) / 200.0f);
    }
}

static void gameInitBgCols() {
    for (int i = 0; i < GAME_MAX_COLS; i++) {
        gameBgCols[i].x = random(280);
        gameBgCols[i].w = 12 + random(20);
        gameBgCols[i].h = 25 + random(50);
        gameBgCols[i].speed = 0.12f + (random(100) / 400.0f);
        uint8_t v = 1 + (int)(gameBgCols[i].speed * 8);
        gameBgCols[i].col = (v << 11) | (v << 6) | v;
    }
}

// ── Game Logic ──────────────────────────────────────────────────
void gameReset() {
    gamePlayerY = GAME_GROUND_Y - GAME_PLAYER_SZ * 2;
    gameVelY = 0;
    gameOnGround = true;
    gameCanDouble = false;
    gamePlayerRot = 0;
    gameSquash = 1.0f;
    gameScore = 0;
    gameSpeed = 2.8f;
    gameSpawnDist = 140;
    gameFlashTimer = 0;
    for (int i = 0; i < GAME_MAX_OBS; i++) gameObs[i].active = false;
    for (int i = 0; i < GAME_MAX_PARTS; i++) gameParts[i].active = false;
    gameInitStars();
    gameInitBgCols();
    gameBgScroll = 0;
    gameUpdateTheme();
    gameFrameMs = millis();
}

static void gameSpawnObstacle() {
    for (int i = 0; i < GAME_MAX_OBS; i++) {
        if (!gameObs[i].active) {
            gameObs[i].x = 245;
            gameObs[i].rot = 0;
            int r = random(100);

            if (gameScore < 5) {
                // Early game: only single spikes
                gameObs[i].type = 0;
            } else if (gameScore < 10) {
                // Introduce double spikes
                gameObs[i].type = (r < 65) ? 0 : 1;
            } else if (gameScore < 18) {
                // Add platforms and tall spikes
                gameObs[i].type = (r < 30) ? 0 : (r < 50) ? 1 : (r < 65) ? 2 : (r < 85) ? 3 : 6;
            } else if (gameScore < 28) {
                // Add ramps
                gameObs[i].type = (r < 20) ? 0 : (r < 35) ? 1 : (r < 48) ? 2 :
                                  (r < 63) ? 3 : (r < 78) ? 4 : (r < 90) ? 6 : 5;
            } else {
                // Full mix with saw blades
                gameObs[i].type = (r < 15) ? 0 : (r < 28) ? 1 : (r < 38) ? 2 :
                                  (r < 52) ? 3 : (r < 65) ? 4 : (r < 78) ? 5 : 6;
            }

            switch (gameObs[i].type) {
                case 0: // single spike
                    gameObs[i].halfW = 7 + random(4);
                    gameObs[i].h = 20 + random(8);
                    break;
                case 1: // double spike
                    gameObs[i].halfW = 14 + random(4);
                    gameObs[i].h = 18 + random(6);
                    break;
                case 2: // tall spike
                    gameObs[i].halfW = 6 + random(3);
                    gameObs[i].h = 30 + random(8);
                    break;
                case 3: // platform (safe to land on)
                    gameObs[i].halfW = 16 + random(10); // wide block
                    gameObs[i].h = 22 + random(12);     // height above ground
                    break;
                case 4: // ramp (auto-launch)
                    gameObs[i].halfW = 14 + random(6);  // base width
                    gameObs[i].h = 16 + random(8);      // ramp height
                    break;
                case 5: // saw blade (spinning, lethal)
                    gameObs[i].halfW = 8 + random(4);   // radius
                    gameObs[i].h = 30 + random(30);     // center height above ground
                    break;
                case 6: // platform with spike on top
                    gameObs[i].halfW = 14 + random(8);
                    gameObs[i].h = 18 + random(10);
                    break;
            }
            gameObs[i].active = true;
            gameObs[i].scored = false;
            return;
        }
    }
}

static void gameDeathExplosion() {
    float px = GAME_PLAYER_X;
    float py = gamePlayerY + GAME_PLAYER_SZ;
    uint16_t cols[] = {gamePlayerCol, gameAccentCol, COL_WHITE, COL_YELLOW, COL_RED};
    for (int i = 0; i < 15; i++) {
        float angle = (random(360)) * 0.01745f;
        float spd = 1.5f + random(100) / 30.0f;
        gameSpawnParticle(px, py, cosf(angle) * spd, sinf(angle) * spd,
                          cols[random(5)], 12 + random(10));
    }
}

// Helper: check spike triangle collision (returns true if hit)
static bool checkSpikeCollision(float pLeft, float pRight, float pBottom,
                                 float tipX, int halfW, int h) {
    float tLeft  = tipX - halfW;
    float tRight = tipX + halfW;
    float tTop   = GAME_GROUND_Y - h;
    if (pRight <= tLeft || pLeft >= tRight || pBottom <= tTop) return false;
    float overlapMid = (max(pLeft, tLeft) + min(pRight, tRight)) / 2.0f;
    float distEdge = min(overlapMid - tLeft, tRight - overlapMid);
    float triTopAtX = GAME_GROUND_Y - h * distEdge / (float)halfW;
    return pBottom > triTopAtX + 4;
}

// Helper: check spike on top of a platform (spike sits at platTop)
static bool checkPlatSpikeCollision(float pLeft, float pRight, float pBottom, float pTop,
                                     float platX, int platHW, int platTop) {
    int spikeH = 12;
    int spikeHW = 5;
    float tipX = platX;
    float sTop = platTop - spikeH;
    float sLeft = tipX - spikeHW, sRight = tipX + spikeHW;
    if (pRight <= sLeft || pLeft >= sRight || pBottom <= sTop || pTop >= platTop) return false;
    float overlapMid = (max(pLeft, sLeft) + min(pRight, sRight)) / 2.0f;
    float distEdge = min(overlapMid - sLeft, sRight - overlapMid);
    float triTopAtX = platTop - spikeH * distEdge / (float)spikeHW;
    return pBottom > triTopAtX + 3;
}

void updateGame() {
    if (gameState != GAME_PLAYING) {
        if (gameState == GAME_OVER) {
            for (int i = 0; i < GAME_MAX_PARTS; i++) {
                if (!gameParts[i].active) continue;
                gameParts[i].x += gameParts[i].vx;
                gameParts[i].y += gameParts[i].vy;
                gameParts[i].vy += 0.3f;
                if (--gameParts[i].life <= 0) gameParts[i].active = false;
            }
        }
        return;
    }

    // ── Physics ──
    gameVelY += GAME_GRAVITY;
    gamePlayerY += gameVelY;

    // Check platform landings (types 3 and 6) — before ground check
    float prevBottom = gamePlayerY + GAME_PLAYER_SZ * 2;
    bool landedOnPlatform = false;
    for (int i = 0; i < GAME_MAX_OBS; i++) {
        if (!gameObs[i].active) continue;
        if (gameObs[i].type != 3 && gameObs[i].type != 6) continue;
        float ox = gameObs[i].x;
        int hw = gameObs[i].halfW;
        int oh = gameObs[i].h;
        float platTop = GAME_GROUND_Y - oh;
        float platLeft = ox - hw, platRight = ox + hw;
        float pLeft = GAME_PLAYER_X - GAME_PLAYER_SZ;
        float pRight = GAME_PLAYER_X + GAME_PLAYER_SZ;

        // Player must overlap horizontally and be falling onto the top
        if (pRight > platLeft + 2 && pLeft < platRight - 2 &&
            gameVelY >= 0 && prevBottom >= platTop && prevBottom < platTop + gameVelY + 6) {
            gamePlayerY = platTop - GAME_PLAYER_SZ * 2;
            if (!gameOnGround) {
                gameLandMs = millis();
                gameSquash = 0.7f;
            }
            gameVelY = 0;
            gameOnGround = true;
            gameCanDouble = false;
            gamePlayerRot = 0;
            landedOnPlatform = true;
            break;
        }
    }

    // Check ramp interaction (type 4) — auto-launch
    for (int i = 0; i < GAME_MAX_OBS; i++) {
        if (!gameObs[i].active || gameObs[i].type != 4) continue;
        float ox = gameObs[i].x;
        int hw = gameObs[i].halfW;
        int oh = gameObs[i].h;
        float rampLeft = ox - hw, rampRight = ox;
        float pCenterX = GAME_PLAYER_X;
        float pBottom = gamePlayerY + GAME_PLAYER_SZ * 2;

        if (pCenterX > rampLeft && pCenterX < rampRight + 4) {
            // Ramp slope: height increases linearly from left (0) to right (oh)
            float frac = (pCenterX - rampLeft) / (float)hw;
            float rampSurfaceY = GAME_GROUND_Y - oh * frac;
            if (pBottom >= rampSurfaceY - 2 && pBottom <= rampSurfaceY + 8 && gameVelY >= 0) {
                // Launch! Stronger than a normal jump
                gameVelY = GAME_JUMP_VEL * 1.2f;
                gameOnGround = false;
                gameCanDouble = true;
                gamePlayerRot = -0.3f;
                // Ramp particles
                for (int p = 0; p < 3; p++) {
                    gameSpawnParticle(pCenterX, pBottom,
                        -1.0f + random(200)/100.0f, -2.0f - random(100)/100.0f,
                        COL_YELLOW, 6);
                }
            }
        }
    }

    // Ground check
    float groundPos = GAME_GROUND_Y - GAME_PLAYER_SZ * 2;
    if (!landedOnPlatform && gamePlayerY >= groundPos) {
        gamePlayerY = groundPos;
        if (!gameOnGround) {
            gameLandMs = millis();
            gameSquash = 0.7f;
        }
        gameVelY = 0;
        gameOnGround = true;
        gameCanDouble = false;
        gamePlayerRot = 0;
    }

    if (!gameOnGround) gamePlayerRot += 0.18f;

    if (gameSquash < 1.0f) {
        gameSquash += 0.06f;
        if (gameSquash > 1.0f) gameSquash = 1.0f;
    }

    // Trail particles
    if (!gameOnGround && random(3) == 0) {
        gameSpawnParticle(
            GAME_PLAYER_X - GAME_PLAYER_SZ + random(4),
            gamePlayerY + GAME_PLAYER_SZ * 2,
            -0.5f - random(100) / 100.0f, 0.5f + random(100) / 100.0f,
            gameAccentCol, 6 + random(4));
    }

    // Update particles
    for (int i = 0; i < GAME_MAX_PARTS; i++) {
        if (!gameParts[i].active) continue;
        gameParts[i].x += gameParts[i].vx;
        gameParts[i].y += gameParts[i].vy;
        gameParts[i].vy += 0.15f;
        if (--gameParts[i].life <= 0) gameParts[i].active = false;
    }

    // Update stars
    for (int i = 0; i < GAME_MAX_STARS; i++) {
        gameStars[i].x -= gameSpeed * gameStars[i].speed;
        if (gameStars[i].x < -2) {
            gameStars[i].x = 242;
            gameStars[i].y = 20 + random(GAME_GROUND_Y - 40);
        }
    }

    // Update bg columns
    for (int i = 0; i < GAME_MAX_COLS; i++) {
        gameBgCols[i].x -= gameSpeed * gameBgCols[i].speed;
        if (gameBgCols[i].x + gameBgCols[i].w < -5) {
            gameBgCols[i].x = 245 + random(40);
            gameBgCols[i].w = 12 + random(20);
            gameBgCols[i].h = 25 + random(50);
        }
    }

    gameBgScroll += gameSpeed;

    // ── Move obstacles & score ──
    for (int i = 0; i < GAME_MAX_OBS; i++) {
        if (!gameObs[i].active) continue;
        gameObs[i].x -= gameSpeed;
        if (gameObs[i].type == 5) gameObs[i].rot += 0.25f; // spin saw blades
        if (gameObs[i].x < -40) { gameObs[i].active = false; continue; }
        if (!gameObs[i].scored && gameObs[i].x < GAME_PLAYER_X - GAME_PLAYER_SZ) {
            gameObs[i].scored = true;
            gameScore++;
            gameSpawnParticle(GAME_PLAYER_X + 20, gamePlayerY, 1.5f, -1.5f, COL_GOLD, 10);
            if (gameScore % 5 == 0) {
                gameSpeed += 0.3f;
                if (gameSpeed > 7.0f) gameSpeed = 7.0f;
            }
            if (gameScore % 10 == 0) {
                gameFlashTimer = 8;
                gameFlashCol = gameAccentCol;
                gameUpdateTheme();
            }
        }
    }

    if (gameFlashTimer > 0) gameFlashTimer--;

    // ── Spawn ──
    gameSpawnDist -= gameSpeed;
    if (gameSpawnDist <= 0) {
        gameSpawnObstacle();
        float minGap = 55 + gameSpeed * 4;
        float maxGap = minGap + 40;
        gameSpawnDist = minGap + random((int)maxGap - (int)minGap);
    }

    // ── Collision detection ──
    int sz = GAME_PLAYER_SZ;
    float pLeft   = GAME_PLAYER_X - sz + 4;
    float pRight  = GAME_PLAYER_X + sz - 4;
    float pTop    = gamePlayerY + 4;
    float pBottom = gamePlayerY + sz * 2 - 2;

    for (int i = 0; i < GAME_MAX_OBS; i++) {
        if (!gameObs[i].active) continue;
        float ox = gameObs[i].x;
        int hw = gameObs[i].halfW;
        int oh = gameObs[i].h;
        bool hit = false;

        switch (gameObs[i].type) {
        case 0: // single spike
        case 2: // tall spike
            hit = checkSpikeCollision(pLeft, pRight, pBottom, ox, hw, oh);
            break;

        case 1: // double spike
            for (int s = -1; s <= 1 && !hit; s += 2) {
                float sx = ox + s * (hw / 2.0f);
                hit = checkSpikeCollision(pLeft, pRight, pBottom, sx, hw / 3, oh);
            }
            break;

        case 3: // platform — side collision only (top is landable)
        {
            float platTop = GAME_GROUND_Y - oh;
            float platLeft = ox - hw, platRight = ox + hw;
            // Side hit: player overlaps horizontally AND vertically but not from top
            if (pRight > platLeft && pLeft < platRight &&
                pBottom > platTop + 6 && pTop < GAME_GROUND_Y) {
                // Only die if hitting the side (not landing from above)
                if (pRight > platLeft && pRight < platLeft + gameSpeed + 4 && pBottom > platTop + 6) {
                    hit = true; // hit left wall
                }
            }
            break;
        }

        case 6: // platform with spike on top
        {
            float platTop = GAME_GROUND_Y - oh;
            float platLeft = ox - hw, platRight = ox + hw;
            // Side collision
            if (pRight > platLeft && pRight < platLeft + gameSpeed + 4 &&
                pBottom > platTop + 6 && pTop < GAME_GROUND_Y) {
                hit = true;
            }
            // Spike on top collision
            if (!hit) {
                hit = checkPlatSpikeCollision(pLeft, pRight, pBottom, pTop,
                                               ox, hw, (int)platTop);
            }
            break;
        }

        case 4: // ramp — side collision (slope is handled in physics above)
        {
            float rampLeft = ox - hw;
            float rampRight = ox;
            float rampTop = GAME_GROUND_Y - oh;
            // Only kill if hitting the vertical right face of the ramp
            if (pRight > rampRight - 2 && pLeft < rampRight + 2 &&
                pBottom > rampTop + 4 && pTop < GAME_GROUND_Y) {
                // Don't kill if player is above the ramp
                if (pBottom > rampTop + oh / 2) hit = true;
            }
            break;
        }

        case 5: // saw blade — circle collision
        {
            float sawCX = ox;
            float sawCY = GAME_GROUND_Y - oh; // center y
            float sawR = hw;
            // Closest point on player rect to circle center
            float closestX = max(pLeft, min(sawCX, pRight));
            float closestY = max(pTop, min(sawCY, pBottom));
            float dx = sawCX - closestX, dy = sawCY - closestY;
            if (dx * dx + dy * dy < (sawR - 2) * (sawR - 2)) {
                hit = true;
            }
            break;
        }
        }

        if (hit) {
            gameState = GAME_OVER;
            if (gameScore > gameHiScore) { gameHiScore = gameScore; saveGameHiScore(); }
            gameDeathExplosion();
            chimeDeath();
            return;
        }
    }
}

// ── Drawing ─────────────────────────────────────────────────────
static void drawGameRotatedSquare(int cx, int cy, int sz, float angle, uint16_t fillCol, uint16_t outCol) {
    float c = cosf(angle), s = sinf(angle);
    int px[4], py[4];
    float offsets[4][2] = {{-1,-1},{1,-1},{1,1},{-1,1}};
    for (int i = 0; i < 4; i++) {
        float ox = offsets[i][0] * sz;
        float oy = offsets[i][1] * sz;
        px[i] = cx + (int)(ox * c - oy * s);
        py[i] = cy + (int)(ox * s + oy * c);
    }
    spr.fillTriangle(px[0], py[0], px[1], py[1], px[2], py[2], fillCol);
    spr.fillTriangle(px[0], py[0], px[2], py[2], px[3], py[3], fillCol);
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        spr.drawLine(px[i], py[i], px[j], py[j], outCol);
    }
}

void drawGameScreen() {
    spr.fillSprite(COL_BG);
    drawCircleBorder();

    if (gameState == GAME_READY) {
        static float titleStarX[8];
        static bool titleInit = false;
        if (!titleInit) {
            for (int i = 0; i < 8; i++) titleStarX[i] = random(240);
            titleInit = true;
        }
        for (int i = 0; i < 8; i++) {
            int sy = 30 + i * 22;
            titleStarX[i] -= 0.5f;
            if (titleStarX[i] < 0) titleStarX[i] = 240;
            spr.drawPixel((int)titleStarX[i], sy, COL_DARK_GRAY);
            spr.drawPixel((int)titleStarX[i] + 1, sy, COL_MID_GRAY);
        }

        bool flash = (millis() / 600) % 2;
        spr.setTextColor(flash ? COL_CYAN : COL_MAGENTA, COL_BG);
        spr.drawString("GEOMETRY", 120, 52, 4);
        spr.setTextColor(flash ? COL_MAGENTA : COL_CYAN, COL_BG);
        spr.drawString("DASH", 120, 80, 4);

        int animY = 135;
        spr.drawFastHLine(40, animY + 14, 160, COL_DARK_GRAY);
        int bounce = abs((int)((millis() / 150) % 12) - 6);
        spr.fillRect(85, animY - bounce, 14, 14, COL_GREEN);
        spr.drawRect(85, animY - bounce, 14, 14, COL_WHITE);
        for (int t = 0; t < 3; t++) {
            spr.fillRect(75 - t * 12, animY + 2 + t, 6 - t, 6 - t, COL_CYAN);
        }
        uint16_t triCols[] = {COL_RED, COL_ORANGE, COL_MAGENTA};
        int triX[] = {120, 145, 165};
        int triH[] = {14, 18, 12};
        for (int t = 0; t < 3; t++) {
            spr.fillTriangle(triX[t], animY + 14 - triH[t],
                             triX[t] - 5, animY + 14,
                             triX[t] + 5, animY + 14, triCols[t]);
        }

        spr.setTextColor(COL_WHITE, COL_BG);
        spr.drawString("Press SET to play", 120, 168, 2);
        spr.setTextColor(COL_MID_GRAY, COL_BG);
        spr.drawString("Double-tap to double jump!", 120, 186, 1);

        if (gameHiScore > 0) {
            spr.setTextColor(COL_GOLD, COL_BG);
            char buf[20];
            sprintf(buf, "Best: %d", gameHiScore);
            spr.drawString(buf, 120, 208, 2);
        }

        drawScreenIndicator(SCREEN_GAME);
        spr.pushSprite(0, 0);
        return;
    }

    if (gameState == GAME_CONFIRM_EXIT) {
        spr.drawCircle(120, 120, 80, COL_DARK_GRAY);
        spr.setTextColor(COL_YELLOW, COL_BG);
        spr.drawString("PAUSED", 120, 65, 4);
        spr.setTextColor(COL_WHITE, COL_BG);
        char buf[20];
        sprintf(buf, "Score: %d", gameScore);
        spr.drawString(buf, 120, 105, 4);
        spr.setTextColor(COL_MID_GRAY, COL_BG);
        char speedBuf[16];
        sprintf(speedBuf, "Speed: %.1f", gameSpeed);
        spr.drawString(speedBuf, 120, 132, 2);
        spr.setTextColor(COL_GREEN, COL_BG);
        spr.drawString("SET = Resume", 120, 162, 2);
        spr.setTextColor(COL_RED, COL_BG);
        spr.drawString("BACK = Quit", 120, 182, 2);
        spr.pushSprite(0, 0);
        return;
    }

    if (gameState == GAME_OVER) {
        for (int i = 0; i < GAME_MAX_PARTS; i++) {
            if (!gameParts[i].active) continue;
            int px = (int)gameParts[i].x, py = (int)gameParts[i].y;
            if (px > 5 && px < 235 && py > 5 && py < 235) {
                int r = (gameParts[i].life > 8) ? 3 : (gameParts[i].life > 4) ? 2 : 1;
                spr.fillCircle(px, py, r, gameParts[i].col);
            }
        }

        bool isNewHi = (gameScore == gameHiScore && gameScore > 0);
        bool flash = (millis() / 300) % 2;
        if (isNewHi) {
            spr.setTextColor(flash ? COL_GOLD : COL_YELLOW, COL_BG);
            spr.drawString("NEW BEST!", 120, 45, 4);
        }
        spr.setTextColor(COL_RED, COL_BG);
        spr.drawString("GAME OVER", 120, 70, 4);
        spr.setTextColor(COL_WHITE, COL_BG);
        char buf[32];
        sprintf(buf, "%d", gameScore);
        spr.drawString(buf, 120, 108, 7);
        if (!isNewHi && gameHiScore > 0) {
            spr.setTextColor(COL_GOLD, COL_BG);
            sprintf(buf, "Best: %d", gameHiScore);
            spr.drawString(buf, 120, 148, 2);
        }
        spr.setTextColor(COL_CYAN, COL_BG);
        spr.drawString("SET = Retry", 120, 178, 2);
        spr.setTextColor(COL_MID_GRAY, COL_BG);
        spr.drawString("BACK = Exit", 120, 196, 2);
        spr.pushSprite(0, 0);
        return;
    }

    // ── GAME_PLAYING ──

    // Tinted background
    if (gameBgCol != 0x0000) {
        for (int y = 20; y < GAME_GROUND_Y; y += 4) {
            int dy = abs(y - 120);
            if (dy > 116) continue;
            int hw = (int)sqrtf(116 * 116 - dy * dy);
            int x0 = max(120 - hw, 4);
            int x1 = min(120 + hw, 236);
            spr.drawFastHLine(x0, y, x1 - x0, gameBgCol);
        }
    }

    // Silhouette columns
    for (int i = 0; i < GAME_MAX_COLS; i++) {
        int bx = (int)gameBgCols[i].x;
        int bw = gameBgCols[i].w;
        int bh = gameBgCols[i].h;
        int by = GAME_GROUND_Y - bh;
        if (bx + bw < 10 || bx > 230) continue;
        spr.fillRect(max(bx, 10), max(by, 20), min(bw, 230 - max(bx, 10)),
                      GAME_GROUND_Y - max(by, 20), gameBgCols[i].col);
        spr.fillRect(max(bx + 1, 10), max(by - 2, 20),
                      max(min(bw - 2, 230 - max(bx + 1, 10)), 0), 2, gameBgCols[i].col);
    }

    // Stars (streaks at high speed)
    for (int i = 0; i < GAME_MAX_STARS; i++) {
        int sx = (int)gameStars[i].x, sy = (int)gameStars[i].y;
        if (sx > 5 && sx < 235 && sy > 15 && sy < GAME_GROUND_Y - 5) {
            if (gameSpeed > 4.5f) {
                int streakLen = (int)(gameSpeed * gameStars[i].speed * 3);
                spr.drawFastHLine(sx, sy, min(streakLen, 235 - sx), gameStars[i].col);
            } else {
                spr.drawPixel(sx, sy, gameStars[i].col);
            }
        }
    }

    // Milestone flash
    if (gameFlashTimer > 0) {
        spr.drawCircle(120, 120, 100 - gameFlashTimer * 5, gameFlashCol);
        spr.drawCircle(120, 120, 101 - gameFlashTimer * 5, gameFlashCol);
    }

    // Ground
    spr.drawFastHLine(15, GAME_GROUND_Y, 210, gameGroundCol);
    spr.drawFastHLine(15, GAME_GROUND_Y + 1, 210, gameGroundCol);

    // Perspective ground lines
    int vpX = 120, vpY = GAME_GROUND_Y - 80;
    int scrollOff = ((int)gameBgScroll) % 30;
    for (int gx = -30 - scrollOff; gx < 260; gx += 30) {
        int fromX = gx;
        int fromY = GAME_GROUND_Y + 12;
        if (fromX > 10 && fromX < 230) {
            spr.drawLine(fromX, fromY, vpX, vpY, gameGroundCol);
        }
    }

    int depthLines[] = {3, 7, 13, 21};
    for (int d = 0; d < 4; d++) {
        int ly = GAME_GROUND_Y + depthLines[d];
        if (ly > 230) continue;
        int lx0 = 15 + d * 8;
        int lx1 = 225 - d * 8;
        spr.drawFastHLine(lx0, ly, lx1 - lx0, gameGroundCol);
    }

    // Particles
    for (int i = 0; i < GAME_MAX_PARTS; i++) {
        if (!gameParts[i].active) continue;
        int px = (int)gameParts[i].x, py = (int)gameParts[i].y;
        if (px > 5 && px < 235 && py > 5 && py < 235) {
            if (gameParts[i].life > 5)
                spr.fillCircle(px, py, 2, gameParts[i].col);
            else
                spr.drawPixel(px, py, gameParts[i].col);
        }
    }

    // Obstacles
    for (int i = 0; i < GAME_MAX_OBS; i++) {
        if (!gameObs[i].active) continue;
        int ox = (int)gameObs[i].x;
        int hw = gameObs[i].halfW;
        int oh = gameObs[i].h;

        switch (gameObs[i].type) {
        case 0: // single spike
            spr.fillTriangle(ox, GAME_GROUND_Y - oh, ox - hw, GAME_GROUND_Y, ox + hw, GAME_GROUND_Y, COL_RED);
            spr.drawLine(ox, GAME_GROUND_Y - oh, ox - hw, GAME_GROUND_Y, COL_ORANGE);
            spr.drawLine(ox, GAME_GROUND_Y - oh, ox + hw, GAME_GROUND_Y, COL_RED);
            if (oh > 22)
                spr.drawLine(ox, GAME_GROUND_Y - oh + 3, ox - hw + 3, GAME_GROUND_Y, COL_ORANGE);
            break;

        case 1: // double spike
        {
            int gap = hw / 2, shw = hw / 3;
            for (int s = -1; s <= 1; s += 2) {
                int sx = ox + s * gap;
                spr.fillTriangle(sx, GAME_GROUND_Y - oh, sx - shw, GAME_GROUND_Y, sx + shw, GAME_GROUND_Y, COL_MAGENTA);
                spr.drawLine(sx, GAME_GROUND_Y - oh, sx - shw, GAME_GROUND_Y, COL_LIGHT_BLUE);
                spr.drawLine(sx, GAME_GROUND_Y - oh, sx + shw, GAME_GROUND_Y, COL_MAGENTA);
            }
            break;
        }

        case 2: // tall spike
            spr.fillTriangle(ox, GAME_GROUND_Y - oh, ox - hw, GAME_GROUND_Y, ox + hw, GAME_GROUND_Y, COL_ORANGE);
            spr.drawLine(ox, GAME_GROUND_Y - oh, ox - hw, GAME_GROUND_Y, COL_YELLOW);
            spr.drawLine(ox, GAME_GROUND_Y - oh, ox + hw, GAME_GROUND_Y, COL_ORANGE);
            spr.drawLine(ox, GAME_GROUND_Y - oh + 3, ox - hw + 3, GAME_GROUND_Y, COL_YELLOW);
            break;

        case 3: // platform (landable block)
        {
            int platTop = GAME_GROUND_Y - oh;
            // Main block
            spr.fillRect(ox - hw, platTop, hw * 2, oh, COL_TEAL);
            spr.drawRect(ox - hw, platTop, hw * 2, oh, COL_CYAN);
            // Top surface highlight
            spr.drawFastHLine(ox - hw + 1, platTop, hw * 2 - 2, COL_GREEN);
            spr.drawFastHLine(ox - hw + 1, platTop + 1, hw * 2 - 2, COL_CYAN);
            // Brick lines
            for (int by = platTop + 8; by < GAME_GROUND_Y; by += 8) {
                spr.drawFastHLine(ox - hw + 2, by, hw * 2 - 4, COL_DARK_GRAY);
            }
            break;
        }

        case 6: // platform with spike on top
        {
            int platTop = GAME_GROUND_Y - oh;
            // Block
            spr.fillRect(ox - hw, platTop, hw * 2, oh, COL_TEAL);
            spr.drawRect(ox - hw, platTop, hw * 2, oh, COL_CYAN);
            spr.drawFastHLine(ox - hw + 1, platTop, hw * 2 - 2, COL_GREEN);
            // Brick lines
            for (int by = platTop + 8; by < GAME_GROUND_Y; by += 8) {
                spr.drawFastHLine(ox - hw + 2, by, hw * 2 - 4, COL_DARK_GRAY);
            }
            // Spike on top
            int spikeH = 12, spikeHW = 5;
            spr.fillTriangle(ox, platTop - spikeH,
                             ox - spikeHW, platTop,
                             ox + spikeHW, platTop, COL_RED);
            spr.drawLine(ox, platTop - spikeH, ox - spikeHW, platTop, COL_ORANGE);
            break;
        }

        case 4: // ramp (right triangle, slope going up-left to right)
        {
            int rampTop = GAME_GROUND_Y - oh;
            // Filled right triangle: left-bottom, right-bottom, right-top
            spr.fillTriangle(ox - hw, GAME_GROUND_Y,  // left base
                             ox, GAME_GROUND_Y,        // right base
                             ox, rampTop,               // right peak
                             COL_GOLD);
            // Slope highlight
            spr.drawLine(ox - hw, GAME_GROUND_Y, ox, rampTop, COL_YELLOW);
            // Right edge
            spr.drawFastVLine(ox, rampTop, oh, COL_ORANGE);
            // Arrow chevrons on slope surface
            for (int c = 0; c < 3; c++) {
                int cx = ox - hw + (hw / 4) * (c + 1);
                float frac = (float)(cx - (ox - hw)) / (float)hw;
                int cy = GAME_GROUND_Y - (int)(oh * frac);
                spr.drawLine(cx - 2, cy + 3, cx, cy, COL_WHITE);
                spr.drawLine(cx, cy, cx + 2, cy + 3, COL_WHITE);
            }
            break;
        }

        case 5: // saw blade (spinning circle)
        {
            int sawCX = ox;
            int sawCY = GAME_GROUND_Y - oh;
            int sawR = hw;
            // Support pole from ground to blade
            spr.drawFastVLine(sawCX, sawCY + sawR, GAME_GROUND_Y - sawCY - sawR, COL_MID_GRAY);
            spr.drawFastVLine(sawCX + 1, sawCY + sawR, GAME_GROUND_Y - sawCY - sawR, COL_DARK_GRAY);
            // Outer circle
            spr.fillCircle(sawCX, sawCY, sawR, COL_MID_GRAY);
            spr.drawCircle(sawCX, sawCY, sawR, COL_RED);
            spr.drawCircle(sawCX, sawCY, sawR - 1, COL_ORANGE);
            // Inner hub
            spr.fillCircle(sawCX, sawCY, 3, COL_WHITE);
            // Spinning teeth/spokes
            float rot = gameObs[i].rot;
            for (int t = 0; t < 6; t++) {
                float a = rot + t * (3.14159f / 3.0f);
                int tx1 = sawCX + (int)(4 * cosf(a));
                int ty1 = sawCY + (int)(4 * sinf(a));
                int tx2 = sawCX + (int)((sawR - 1) * cosf(a));
                int ty2 = sawCY + (int)((sawR - 1) * sinf(a));
                spr.drawLine(tx1, ty1, tx2, ty2, COL_RED);
            }
            // Teeth notches on edge
            for (int t = 0; t < 8; t++) {
                float a = rot + t * (3.14159f / 4.0f);
                int nx = sawCX + (int)((sawR + 2) * cosf(a));
                int ny = sawCY + (int)((sawR + 2) * sinf(a));
                spr.fillCircle(nx, ny, 2, COL_RED);
            }
            break;
        }
        } // switch
    }

    // Player
    int py = (int)gamePlayerY;
    int sz = GAME_PLAYER_SZ;
    int cx = GAME_PLAYER_X;
    int cy = py + sz;

    int drawW = (int)(sz * (2.0f - gameSquash));
    int drawH = (int)(sz * gameSquash);

    if (!gameOnGround) {
        drawGameRotatedSquare(cx, cy, sz, gamePlayerRot, gamePlayerCol, COL_WHITE);
        float ec = cosf(gamePlayerRot), es = sinf(gamePlayerRot);
        int ex = cx + (int)(4 * ec - (-4) * es);
        int ey = cy + (int)(4 * es + (-4) * ec);
        spr.fillCircle(ex, ey, 3, COL_BG);
        spr.fillCircle(ex + 1, ey, 2, COL_WHITE);
        if (gameCanDouble) {
            spr.drawCircle(cx, cy, sz + 4, gameAccentCol);
        }
    } else {
        int rx = cx - drawW;
        int ry = cy - drawH + (sz - drawH);
        spr.fillRect(rx, ry, drawW * 2, drawH * 2, gamePlayerCol);
        spr.drawRect(rx, ry, drawW * 2, drawH * 2, COL_WHITE);
        spr.fillRect(cx + 2, ry + 3, 5, 5, COL_BG);
        spr.fillRect(cx + 3, ry + 4, 3, 3, COL_WHITE);
    }

    // Score
    if (gameFlashTimer > 0 && gameFlashTimer % 2 == 0) {
        spr.setTextColor(gameFlashCol, COL_BG);
    } else {
        spr.setTextColor(COL_WHITE, COL_BG);
    }
    char scoreBuf[16];
    sprintf(scoreBuf, "%d", gameScore);
    spr.drawString(scoreBuf, 120, 22, 4);

    // Speed bar
    int barW = min((int)(gameSpeed * 5), 40);
    uint16_t barCol = (gameSpeed < 4) ? COL_GREEN : (gameSpeed < 5.5f) ? COL_YELLOW : COL_RED;
    spr.fillRect(195 - barW, 12, barW, 4, barCol);
    spr.drawRect(155, 12, 40, 4, COL_DARK_GRAY);

    spr.pushSprite(0, 0);
}
