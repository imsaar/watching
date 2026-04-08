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

#define GAME_MAX_OBS     6
#define GAME_MAX_PARTS  20
#define GAME_MAX_STARS  18
#define GAME_MAX_COLS    6

struct Obstacle {
    float x;
    int   halfW, h, type;
    bool  active, scored;
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
            int r = random(100);
            if (gameScore < 5) {
                gameObs[i].type = 0;
            } else if (gameScore < 15) {
                gameObs[i].type = (r < 70) ? 0 : 1;
            } else {
                gameObs[i].type = (r < 40) ? 0 : (r < 75) ? 1 : 2;
            }
            switch (gameObs[i].type) {
                case 0: gameObs[i].halfW = 7 + random(4); gameObs[i].h = 20 + random(8); break;
                case 1: gameObs[i].halfW = 14 + random(4); gameObs[i].h = 18 + random(6); break;
                case 2: gameObs[i].halfW = 6 + random(3); gameObs[i].h = 30 + random(8); break;
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

    gameVelY += GAME_GRAVITY;
    gamePlayerY += gameVelY;
    float groundPos = GAME_GROUND_Y - GAME_PLAYER_SZ * 2;
    if (gamePlayerY >= groundPos) {
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

    if (!gameOnGround && random(3) == 0) {
        gameSpawnParticle(
            GAME_PLAYER_X - GAME_PLAYER_SZ + random(4),
            gamePlayerY + GAME_PLAYER_SZ * 2,
            -0.5f - random(100) / 100.0f, 0.5f + random(100) / 100.0f,
            gameAccentCol, 6 + random(4));
    }

    for (int i = 0; i < GAME_MAX_PARTS; i++) {
        if (!gameParts[i].active) continue;
        gameParts[i].x += gameParts[i].vx;
        gameParts[i].y += gameParts[i].vy;
        gameParts[i].vy += 0.15f;
        if (--gameParts[i].life <= 0) gameParts[i].active = false;
    }

    for (int i = 0; i < GAME_MAX_STARS; i++) {
        gameStars[i].x -= gameSpeed * gameStars[i].speed;
        if (gameStars[i].x < -2) {
            gameStars[i].x = 242;
            gameStars[i].y = 20 + random(GAME_GROUND_Y - 40);
        }
    }

    for (int i = 0; i < GAME_MAX_COLS; i++) {
        gameBgCols[i].x -= gameSpeed * gameBgCols[i].speed;
        if (gameBgCols[i].x + gameBgCols[i].w < -5) {
            gameBgCols[i].x = 245 + random(40);
            gameBgCols[i].w = 12 + random(20);
            gameBgCols[i].h = 25 + random(50);
        }
    }

    gameBgScroll += gameSpeed;

    for (int i = 0; i < GAME_MAX_OBS; i++) {
        if (!gameObs[i].active) continue;
        gameObs[i].x -= gameSpeed;
        if (gameObs[i].x < -30) { gameObs[i].active = false; continue; }
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

    gameSpawnDist -= gameSpeed;
    if (gameSpawnDist <= 0) {
        gameSpawnObstacle();
        float minGap = 55 + gameSpeed * 4;
        float maxGap = minGap + 40;
        gameSpawnDist = minGap + random((int)maxGap - (int)minGap);
    }

    // Collision detection
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

        if (gameObs[i].type == 1) {
            for (int s = -1; s <= 1; s += 2) {
                float sx = ox + s * (hw / 2.0f);
                int shw = hw / 3;
                float tLeft = sx - shw, tRight = sx + shw;
                float tTop = GAME_GROUND_Y - oh;
                if (pRight <= tLeft || pLeft >= tRight || pBottom <= tTop) continue;
                float overlapMid = (max(pLeft, tLeft) + min(pRight, tRight)) / 2.0f;
                float distEdge = min(overlapMid - tLeft, tRight - overlapMid);
                float triTopAtX = GAME_GROUND_Y - oh * distEdge / (float)shw;
                if (pBottom > triTopAtX + 4) {
                    gameState = GAME_OVER;
                    if (gameScore > gameHiScore) { gameHiScore = gameScore; saveGameHiScore(); }
                    gameDeathExplosion();
                    chimeDeath();
                    return;
                }
            }
        } else {
            float tLeft  = ox - hw;
            float tRight = ox + hw;
            float tTop   = GAME_GROUND_Y - oh;
            if (pRight <= tLeft || pLeft >= tRight || pBottom <= tTop) continue;
            float overlapMid = (max(pLeft, tLeft) + min(pRight, tRight)) / 2.0f;
            float distEdge = min(overlapMid - tLeft, tRight - overlapMid);
            float triTopAtX = GAME_GROUND_Y - oh * distEdge / (float)hw;
            if (pBottom > triTopAtX + 4) {
                gameState = GAME_OVER;
                if (gameScore > gameHiScore) { gameHiScore = gameScore; saveGameHiScore(); }
                gameDeathExplosion();
                chimeDeath();
                return;
            }
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

        uint16_t obsCol, obsHighlight;
        switch (gameObs[i].type) {
            case 0: obsCol = COL_RED;     obsHighlight = COL_ORANGE;  break;
            case 1: obsCol = COL_MAGENTA; obsHighlight = COL_LIGHT_BLUE; break;
            case 2: obsCol = COL_ORANGE;  obsHighlight = COL_YELLOW;  break;
            default: obsCol = COL_RED;    obsHighlight = COL_ORANGE;  break;
        }

        if (gameObs[i].type == 1) {
            int gap = hw / 2;
            int shw = hw / 3;
            for (int s = -1; s <= 1; s += 2) {
                int sx = ox + s * gap;
                spr.fillTriangle(sx, GAME_GROUND_Y - oh, sx - shw, GAME_GROUND_Y, sx + shw, GAME_GROUND_Y, obsCol);
                spr.drawLine(sx, GAME_GROUND_Y - oh, sx - shw, GAME_GROUND_Y, obsHighlight);
                spr.drawLine(sx, GAME_GROUND_Y - oh, sx + shw, GAME_GROUND_Y, obsCol);
            }
        } else {
            spr.fillTriangle(ox, GAME_GROUND_Y - oh, ox - hw, GAME_GROUND_Y, ox + hw, GAME_GROUND_Y, obsCol);
            spr.drawLine(ox, GAME_GROUND_Y - oh, ox - hw, GAME_GROUND_Y, obsHighlight);
            spr.drawLine(ox, GAME_GROUND_Y - oh, ox + hw, GAME_GROUND_Y, obsCol);
            if (oh > 22) {
                spr.drawLine(ox, GAME_GROUND_Y - oh + 3, ox - hw + 3, GAME_GROUND_Y, obsHighlight);
            }
        }
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
