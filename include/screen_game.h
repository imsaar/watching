#ifndef SCREEN_GAME_H
#define SCREEN_GAME_H

#include <Arduino.h>

// Game constants
#define GAME_GROUND_Y   178
#define GAME_PLAYER_X    55
#define GAME_PLAYER_SZ   14
#define GAME_GRAVITY    1.0f
#define GAME_JUMP_VEL  -8.8f

enum GameState { GAME_READY, GAME_PLAYING, GAME_OVER, GAME_CONFIRM_EXIT };

extern GameState gameState;
extern float gamePlayerY;
extern float gameVelY;
extern bool  gameOnGround;
extern bool  gameCanDouble;
extern float gamePlayerRot;
extern float gameSpeed;
extern int   gameScore;
extern int   gameHiScore;
extern unsigned long gameFrameMs;
extern uint16_t gameAccentCol;
extern uint16_t gameGroundCol;
extern uint16_t gamePlayerCol;

void drawGameScreen();
void updateGame();
void gameReset();
void gameSpawnParticle(float x, float y, float vx, float vy, uint16_t col, int life);

#endif
