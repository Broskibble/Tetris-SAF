//#define SAF_SETTING_FORCE_1BIT 1

#define SAF_SETTING_BACKGROUND_COLOR 0xe0

#define SAF_PROGRAM_NAME "Tetris"

#define SAF_SETTING_FASTER_1BIT 2
#define SAF_SETTING_ENABLE_SOUND 1 
#define SAF_SETTING_ENABLE_SAVES 1
#define SAF_SETTING_BACKGROUND_COLOR 0
//#define SAF_SETTING_FORCE_1BIT 1

#include "saf.h"

/* #define GAMES 5 ///< number of minigames

static const char *gameNames[GAMES] =
  {
    "SNEK",
    "MINE",
    "Tetris",
    "2048",
    "RUNR"
  }; */

#define MEMORY_SIZE 512
#define MEMORY_VARIABLE_AREA ((32 * 29) / 2) // = 464

#define BUTTON_HOLD_PERIOD 18

void (*stepFunction) (void);

void menuStep(void);

/** Memory that's used by the games: the memory is shared between games and at
  any time at most one game is using the memory. This helps save RAM on
  platforms that don't have much of it. The memory has two parts:

  - array area (first MEMORY_VARIABLE_AREA bytes): used for 2D array data, the
    size e.g. allows to store 32 * 29 half-byte values.
  - variable area: game's global variables should be mapped into this area */
uint8_t memory[MEMORY_SIZE];

#define VAR(type, index) (*((type *) (memory + MEMORY_VARIABLE_AREA + index)))

void clearMemory()
{
  uint8_t *m = memory;

  for (uint16_t i = 0; i < MEMORY_SIZE; ++i, ++m)
    *m = 0;
}

void setMemoryHalfByte(uint16_t index, uint8_t data)
{
  uint8_t *m = memory + index / 2;
  *m = (index % 2) ? ((*m & 0xf0) | data) : ((*m & 0x0f) | (data << 4));
}

uint8_t getMemoryHalfByte(uint16_t index)
{
  return (index % 2) ? (memory[index / 2] & 0x0f) : (memory[index / 2] >> 4);
}

uint8_t buttonPressedOrHeld(uint8_t key)
{
  uint8_t b = SAF_buttonPressed(key);
  return (b == 1) || (b >= BUTTON_HOLD_PERIOD);
}

void drawTextRightAlign(int8_t x, int8_t y, const char *text, uint8_t color,
  uint8_t size)
{
  uint8_t l = 0;

  while (text[l] != 0)
    l++;

  x = x - l * 5 * size + 1;

  SAF_drawText(text,x,y,color,size);
}

void blinkHighScore()
{
  if (SAF_frame() & 0x10)
  {
    SAF_drawRect(5,20,54,16,SAF_COLOR_GRAY_DARK,1);
    SAF_drawText("HISCORE!",11,25,SAF_COLOR_GREEN,1);
  }
}

void saveHiScore(uint8_t index, uint16_t score)
{
  SAF_save(index * 2,score & 0x00ff);
  SAF_save(index * 2 + 1,score / 256);
}

uint16_t getHiScore(uint8_t index)
{
  return SAF_load(index * 2) + (((uint16_t) SAF_load(index * 2 + 1)) * 256);
}


// BLOCKS / TETRIS ----------------------------------------------------------------------

/* square format is following:

   MSB 76543210 LSB

   012:  block type/color, 0 = empty square
   3456: for rotation, says the position of the block within the tetromino 
   7:    1 for an active (falling) block, 0 otherwise 

   the square with value 0xff is a flashing square to be removed */

#define BLOCKS_BOARD_W 10
#define BLOCKS_BOARD_H 15
#define BLOCKS_BOARD_SQUARES (BLOCKS_BOARD_W * BLOCKS_BOARD_H)
#define BLOCKS_SQUARE_SIZE 4
#define BLOCKS_BLOCK_TYPES 8
#define BLOCKS_LINE_SCORE 10
#define BLOCKS_LAND_SCORE 1
#define BLOCKS_LEVEL_DURATION (SAF_FPS * 60)
#define BLOCKS_SPEED_INCREASE 3
#define BLOCKS_START_SPEED 25
#define BLOCKS_SAVE_SLOT 2

#define BLOCKS_OFFSET_X \
  (SAF_SCREEN_WIDTH - BLOCKS_BOARD_W * BLOCKS_SQUARE_SIZE)

#define BLOCKS_OFFSET_Y 2

#define BLOCKS_STATE VAR(uint8_t,0)
#define BLOCKS_SPEED VAR(uint8_t,1)
#define BLOCKS_NEXT_MOVE VAR(uint8_t,2)
#define BLOCKS_WAIT_TIMER VAR(uint8_t,3)
#define BLOCKS_LEVEL VAR(uint8_t,4)
#define BLOCKS_SCORE VAR(uint16_t,5)
#define BLOCKS_NEXT_LEVEL_IN VAR(uint32_t,7)

#define BLOCKS_HISCORE VAR(uint8_t,0)

void quitGame(void)
{
  stepFunction = &menuStep;
    SAF_playSound(SAF_SOUND_CLICK);
}

uint8_t blocksSquareIsSolid(uint8_t square)
{
  uint8_t val = memory[square];
  return (val != 0) && (val & 0x80) == 0;
}

uint8_t blocksSpawnBlock(uint8_t type)
{
  if (type == 0)
    type = 7;

  uint8_t s[4] = {4,5,14,15}; // start with square tetromino
  uint8_t v[4] = {6,6,6,6};   // all center squares, unrotatable

  /* the v array holds the position of the tetromino like this:

       0
      123
     45678
      9ab
       c */

  switch (type) // modify to a specific shape
  {
    case 1: // reverse L
      s[0] = 3; s[1] = 13; 
      v[0] = 1; v[1] = 5; v[3] = 7;  
      break; 

    case 2: // L
      s[0] = 13;
      v[0] = 5; v[1] = 3; v[3] = 7;
      break; 

    case 3: // S
      s[0] = 6;  
      v[0] = 3; v[1] = 2; v[2] = 5; v[3] = 6;
      break; 

    case 4: // Z
      s[2] = 16;
      v[0] = 1; v[1] = 2; v[2] = 7; v[3] = 6;
      break;

    case 5: // upside-down T
      s[0] = 16;
      v[0] = 7; v[1] = 2; v[2] = 5; v[3] = 6; 
      break; 

    case 6: // I
      s[2] = 3; s[3] = 6; 
      v[1] = 7; v[2] = 5; v[3] = 8;
      break; 

    default: break;
  }

  type |= 0x80;

  uint8_t result = 1;

  for (uint8_t i = 0; i < 4; ++i)
  {
    uint8_t square = s[i];

    if (memory[square] != 0)
      result = 0;

    memory[square] = type | ((v[i] << 3));
  }

  return result;
}

void blocksInit(void)
{
  clearMemory();

  BLOCKS_SPEED = BLOCKS_START_SPEED;
  BLOCKS_NEXT_MOVE = BLOCKS_START_SPEED;
  BLOCKS_WAIT_TIMER = 0;
  BLOCKS_LEVEL = 0;
  BLOCKS_SCORE = 0;
  BLOCKS_NEXT_LEVEL_IN = 0;
  BLOCKS_STATE = 0;

  blocksSpawnBlock(SAF_random() % BLOCKS_BLOCK_TYPES);
}

void blocksRotate(uint8_t left)
{
  const int8_t rotationMap[13 * 2] =
    /* old    new  square offset */
    {/* 0 */  8,   2 * BLOCKS_BOARD_W + 2,
     /* 1 */  3,   2,
     /* 2 */  7,   BLOCKS_BOARD_W + 1,
     /* 3 */  11,  2 * BLOCKS_BOARD_W,
     /* 4 */  0,   -2 * BLOCKS_BOARD_W + 2,
     /* 5 */  2,   -1 * BLOCKS_BOARD_W + 1,
     /* 6 */  6,   0,
     /* 7 */  10,  BLOCKS_BOARD_W - 1,
     /* 8 */  12,  2 * BLOCKS_BOARD_W - 2,
     /* 9 */  1,   -2 * BLOCKS_BOARD_W,
     /* 10*/  5,   -1 * BLOCKS_BOARD_W - 1,
     /* 11*/  9,   -2,
     /* 12*/  4,   -2 * BLOCKS_BOARD_W - 2};

  uint8_t blocksProcessed = 0;
  uint8_t newPositions[4];
  uint8_t newValues[4];

  for (uint8_t i = 0; i < BLOCKS_BOARD_SQUARES; ++i)
  {
    uint8_t square = memory[i];

    if (square & 0x80)
    {

      uint8_t index = 2 * ((square >> 3) & 0x0f);

      uint8_t newPos = i + rotationMap[index + 1];

      if (left)
      {
        // rotate two more times

        index = rotationMap[index] * 2;
        newPos += rotationMap[index + 1];

        index = rotationMap[index] * 2;
        newPos += rotationMap[index + 1];
      }

      int xDiff = (newPos % BLOCKS_BOARD_W) - (i % BLOCKS_BOARD_W);

      xDiff = xDiff >= 0 ? xDiff : (-1 * xDiff);

      if ((xDiff > 2) || // left/right outside?
          (newPos >= BLOCKS_BOARD_SQUARES) || // top/bottom outside?
          blocksSquareIsSolid(newPos))
        return; // can't rotate

      newValues[blocksProcessed] = (square & 0x87) | (rotationMap[index] << 3);
      newPositions[blocksProcessed] = newPos;

      blocksProcessed++;

      if (blocksProcessed >= 4)
        break;
    }
  }

  for (uint8_t i = 0; i < BLOCKS_BOARD_SQUARES; ++i)
    if (memory[i] & 0x80)
      memory[i] = 0;

  for (uint8_t i = 0; i < 4; ++i)
    memory[newPositions[i]] = newValues[i];
}

void blocksDraw(void)
{
  SAF_clearScreen(BLOCKS_STATE ? SAF_COLOR_RED_DARK : SAF_COLOR_GRAY_DARK);

  SAF_drawRect(
#if SAF_PLATFORM_COLOR_COUNT > 2
    BLOCKS_OFFSET_X,BLOCKS_OFFSET_Y,
    BLOCKS_BOARD_W * BLOCKS_SQUARE_SIZE, BLOCKS_BOARD_H * BLOCKS_SQUARE_SIZE,
#else
    BLOCKS_OFFSET_X - 1,BLOCKS_OFFSET_Y- 1,BLOCKS_BOARD_W * BLOCKS_SQUARE_SIZE 
    + 2, BLOCKS_BOARD_H * BLOCKS_SQUARE_SIZE + 2,
#endif
    SAF_COLOR_WHITE,1);

  char text[16] = "L ";

  SAF_intToStr(BLOCKS_LEVEL,text + 1);
  SAF_drawText(text,2,3,SAF_COLOR_WHITE,1);
  SAF_intToStr(BLOCKS_SCORE,text);
  SAF_drawText(text,2,10,SAF_COLOR_WHITE,1);

  uint8_t x = 0, y = 0;

  for (uint8_t i = 0; i < BLOCKS_BOARD_SQUARES; ++i)
  {
    uint8_t square = memory[i]; 

    if (square)
    {
      if (square != 0xff)
        square &= 0x07;

      uint8_t color = 0;

#if SAF_PLATFORM_COLOR_COUNT > 2
      switch (square)
      {
        case 1: color = SAF_COLOR_RED; break;
        case 2: color = SAF_COLOR_GREEN; break;
        case 3: color = SAF_COLOR_BROWN; break;
        case 4: color = SAF_COLOR_YELLOW; break;
        case 5: color = SAF_COLOR_ORANGE; break;
        case 6: color = SAF_COLOR_BLUE; break;
        case 7: color = SAF_COLOR_GREEN_DARK; break;
        case 0xff: color = ((SAF_frame() >> 2) & 0x01) ? SAF_COLOR_BLACK : SAF_COLOR_WHITE; break;
        default: break;
      }
#else
      color = (square != 0xff) ? SAF_COLOR_BLACK :
        ((SAF_frame() >> 2) & 0x01) ? SAF_COLOR_BLACK : SAF_COLOR_WHITE;
#endif

      uint8_t
        drawX = BLOCKS_OFFSET_X + x * BLOCKS_SQUARE_SIZE,
        drawY = BLOCKS_OFFSET_Y + y * BLOCKS_SQUARE_SIZE;

      SAF_drawRect(drawX,drawY,BLOCKS_SQUARE_SIZE,BLOCKS_SQUARE_SIZE,color,1);
    }

    x++;

    if (x >= 10)
    {
      x = 0;
      y++;
    }
  }

  if (BLOCKS_STATE && BLOCKS_SCORE == getHiScore(BLOCKS_SAVE_SLOT))
    blinkHighScore();
}

uint8_t blocksFallStep(void)
{
  uint8_t canFall = 1;

  for (uint8_t i = 0; i < BLOCKS_BOARD_SQUARES; ++i)
    if (memory[i] & 0x80)
    {
      if (i / BLOCKS_BOARD_W == BLOCKS_BOARD_H - 1)
      {
        canFall = 0;
        break;
      }

      if (blocksSquareIsSolid(i + BLOCKS_BOARD_W))
      {
        canFall = 0;
        break;
      }
    }

  if (canFall)
  {
    for (uint8_t i = BLOCKS_BOARD_W * (BLOCKS_BOARD_H - 1) - 1; i != 255; --i)
      if (memory[i] & 0x80)
      {
        memory[i + BLOCKS_BOARD_W] = memory[i];
        memory[i] = 0;
      }
  }
 
  return canFall; 
}

void blocksMoveHorizontally(uint8_t left)
{
  uint8_t limitCol = left ? 0 : (BLOCKS_BOARD_W - 1);
  int8_t increment = left ? -1 : 1;

  for (uint8_t i = 0; i < BLOCKS_BOARD_SQUARES; ++i)
    if ((memory[i] & 0x80) && 
        (
          (i % BLOCKS_BOARD_W == limitCol) ||
          blocksSquareIsSolid(i + increment)
        ))
      return;

  uint8_t i0 = 0, i1 = BLOCKS_BOARD_SQUARES;

  if (!left)
  {
    i0 = BLOCKS_BOARD_SQUARES - 1;
    i1 = 255;
  }

  for (uint8_t i = i0; i != i1; i -= increment)
    if (memory[i] & 0x80)
    {
      memory[i + increment] = memory[i];
      memory[i] = 0;
    }
}

void blocksRemoveLines(void)
{
  for (uint8_t i = 0; i < BLOCKS_BOARD_SQUARES; ++i)
    if (memory[i] == 0xff)
    {
      BLOCKS_SCORE += BLOCKS_LINE_SCORE;

      for (uint8_t j = i + BLOCKS_BOARD_W - 1; j >= BLOCKS_BOARD_W; --j)
        memory[j] = memory[j - BLOCKS_BOARD_W];

      for (uint8_t j = 0; j < BLOCKS_BOARD_W; ++j)
        memory[j] = 0;
    } 
}

void blocksStep(void)
{
  if (SAF_buttonPressed(SAF_BUTTON_C) >= BUTTON_HOLD_PERIOD)
    quitGame();

  blocksDraw();

  if (BLOCKS_STATE == 1)
  {
    if (SAF_buttonJustPressed(SAF_BUTTON_A) ||
      SAF_buttonJustPressed(SAF_BUTTON_B))
    {
      blocksInit();
      BLOCKS_STATE = 0;
    }

    return;
  }

  if (BLOCKS_NEXT_LEVEL_IN == 0)
  {
    BLOCKS_LEVEL += 1;
    BLOCKS_NEXT_LEVEL_IN = BLOCKS_LEVEL_DURATION;

    if (BLOCKS_SPEED > BLOCKS_SPEED_INCREASE)
      BLOCKS_SPEED -= BLOCKS_SPEED_INCREASE;
  }

  BLOCKS_NEXT_LEVEL_IN -= 1;

  if (BLOCKS_WAIT_TIMER > 0)
  {
    BLOCKS_WAIT_TIMER -= 1;

    if (BLOCKS_WAIT_TIMER == 1)
      blocksRemoveLines();

    return;
  }

  BLOCKS_NEXT_MOVE = BLOCKS_NEXT_MOVE - 1;

  if (SAF_buttonJustPressed(SAF_BUTTON_DOWN))
  {
    // drop the block:

    while (blocksFallStep());
    
    SAF_playSound(SAF_SOUND_BUMP);

    BLOCKS_NEXT_MOVE = 0;
  }

  if (buttonPressedOrHeld(SAF_BUTTON_LEFT))
    blocksMoveHorizontally(1);
  else if (buttonPressedOrHeld(SAF_BUTTON_RIGHT))
    blocksMoveHorizontally(0);

  if (SAF_buttonJustPressed(SAF_BUTTON_A))
    blocksRotate(0);
  else if (SAF_buttonJustPressed(SAF_BUTTON_B))
    blocksRotate(1);

  if (BLOCKS_NEXT_MOVE == 0)
  {
    if (!blocksFallStep())
    {
      for (uint8_t i = 0; i < BLOCKS_BOARD_SQUARES; ++i)
        memory[i] &= 0x07;

      // scan for completed lines:

      uint8_t col = 0;
      uint8_t count = 0;
      uint8_t lineCompleted = 0;

      for (uint8_t i = 0; i < BLOCKS_BOARD_SQUARES; ++i)
      {
        if (memory[i] != 0)
          count++;

        col++;

        if (col >= BLOCKS_BOARD_W)
        {
          if (count >= BLOCKS_BOARD_W)
          {
            lineCompleted = 1;

            for (uint8_t j = i - BLOCKS_BOARD_W + 1; j <= i; ++j)
              memory[j] = 0xff; 
          }

          col = 0;
          count = 0;
        }
      }

      if (lineCompleted)
      {
        BLOCKS_WAIT_TIMER = 20;
          SAF_playSound(SAF_SOUND_BEEP);
      }

      BLOCKS_SCORE += 1;

      if (!blocksSpawnBlock(SAF_random() % BLOCKS_BLOCK_TYPES))
      {
        BLOCKS_STATE = 1; // game over
        
        SAF_playSound(SAF_SOUND_BOOM);

        if (BLOCKS_SCORE >= getHiScore(BLOCKS_SAVE_SLOT))
          saveHiScore(BLOCKS_SAVE_SLOT,BLOCKS_SCORE);
		  BLOCKS_HISCORE = BLOCKS_SCORE;
      }
    }

    BLOCKS_NEXT_MOVE = BLOCKS_SPEED;
  }
}

//-//////////
uint8_t menuItem = 0;
uint8_t firstClick = 0;

void menuStep(void)
{
  SAF_clearScreen(SAF_COLOR_BLACK);  
  // SAF_drawCircle(13,47,14,SAF_COLOR_GRAY_DARK,1);
  SAF_drawRect(0,(SAF_SCREEN_HEIGHT/5)-3,SAF_SCREEN_WIDTH,6,SAF_COLOR_GREEN_DARK,1);

  SAF_drawRect(0,29,SAF_SCREEN_WIDTH,6,SAF_COLOR_BLUE,1);

// #if SAF_PLATFORM_COLOR_COUNT > 2
//  SAF_drawText("TETRIS",12,25,SAF_COLOR_WHITE,2);
//#endif

  SAF_drawText("TET",SAF_SCREEN_WIDTH/6,SAF_SCREEN_HEIGHT/2 - 11,SAF_COLOR_WHITE,2);
  SAF_drawText("RIS",SAF_SCREEN_WIDTH/6,(SAF_SCREEN_HEIGHT/2) + 3,SAF_COLOR_WHITE,2);

  uint16_t score = BLOCKS_HISCORE;

  char scoreText[6] = "XXX";

  if (score != 0)
  {
    SAF_intToStr(score,scoreText);
  }
  
  // SAF_drawText(scoreText,score < 10000 ? 6 : 1,SAF_SCREEN_HEIGHT - (SAF_SCREEN_HEIGHT/4),SAF_COLOR_WHITE,1);

  SAF_drawText(scoreText,SAF_SCREEN_WIDTH/6,(SAF_SCREEN_HEIGHT/6) ,SAF_COLOR_WHITE,1);

  if (SAF_buttonPressed(SAF_BUTTON_A))
  {
    if (!firstClick)
    {
      SAF_randomSeed(SAF_frame()); // create a somewhat random initial seed
      firstClick = 1;
    }
	
	blocksInit(); stepFunction = &blocksStep;
	
      SAF_playSound(SAF_SOUND_CLICK);
  }
}

void SAF_init(void)
{
  stepFunction = &menuStep;
}

uint8_t SAF_loop(void)
{
  stepFunction();
  return 1;
}
