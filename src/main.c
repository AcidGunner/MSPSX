#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <psxgpu.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <psxapi.h>
#include <psxpad.h>
#include <psxspu.h>

#include "pad.h"

#define OT_LENGTH 16

#define BUFFER_LENGTH 8192

typedef struct
{
	DISPENV disp_env;
	DRAWENV draw_env;

	uint32_t ot[OT_LENGTH];
	uint8_t buffer[BUFFER_LENGTH];
} RenderBuffer;

#define MAX_W 24
#define MAX_H 16

typedef struct
{
	uint8_t mine;
	uint8_t revealed;
	uint8_t flagged;
	int neighbors;
	bool hasNeighborMines;
	//uint8_t processed;
} Cell;
Cell board[MAX_W][MAX_H];

int GRID_W = 9;
int GRID_H = 9;
int MINES = 10;
int mine_count = 0;

int cursorX = 0;
int cursorY = 0;

void GenerateBoard()
{
	int xx, yy;
	int active_count = 0;
    memset(board, 0, sizeof(board));
	
	mine_count = MINES;
	
    int placed = 0;
    while(placed < MINES)
    {
        int x = rand() % GRID_W;
        int y = rand() % GRID_H;
		if (board[x][y].mine) continue;

		board[x][y].mine = 1;
		placed++;
    }
}

void CalculateNumbers()
{
	int x, y;
    for(x=0;x<GRID_W;x++)
    {
        for(y=0;y<GRID_H;y++)
        {
            if(board[x][y].mine) continue;
			int ox, oy;
            int count = 0;
			bool hasMine = false;

            for(ox=-1;ox<=1;ox++)
            {
                for(oy=-1;oy<=1;oy++)
                {
					int nx, ny;
					
                    nx = x + ox;
                    ny = y + oy;

                    if(nx < 0 || ny < 0 || nx >= GRID_W || ny >= GRID_H) continue;
					
                    if(board[nx][ny].mine)
					{
						count += 1;
						hasMine = true;
					}
                }
            }
            board[x][y].neighbors = count;
            board[x][y].hasNeighborMines = hasMine;
        }
    }
}

typedef struct
{
	RenderBuffer buffers[2];
	uint8_t      *next_packet;
	int          active_buffer;
} RenderContext;

void setup_context(RenderContext *ctx, int w, int h, int r, int g, int b)
{
	SetDefDrawEnv(&(ctx->buffers[0].draw_env), 0, 0, w, h);
	SetDefDispEnv(&(ctx->buffers[0].disp_env), 0, 0, w, h);
	SetDefDrawEnv(&(ctx->buffers[1].draw_env), 0, h, w, h);
	SetDefDispEnv(&(ctx->buffers[1].disp_env), 0, h, w, h);

	setRGB0(&(ctx->buffers[0].draw_env), r, g, b);
	setRGB0(&(ctx->buffers[1].draw_env), r, g, b);
	ctx->buffers[0].draw_env.isbg = 1;
	ctx->buffers[1].draw_env.isbg = 1;

	ctx->active_buffer = 0;
	ctx->next_packet   = ctx->buffers[0].buffer;
	ClearOTagR(ctx->buffers[0].ot, OT_LENGTH);

	SetDispMask(1);
}

void flip_buffers(RenderContext *ctx)
{
	DrawSync(0);
	VSync(0);

	RenderBuffer *draw_buffer = &(ctx->buffers[ctx->active_buffer]);
	RenderBuffer *disp_buffer = &(ctx->buffers[ctx->active_buffer ^ 1]);

	PutDispEnv(&(disp_buffer->disp_env));
	DrawOTagEnv(&(draw_buffer->ot[OT_LENGTH - 1]), &(draw_buffer->draw_env));

	ctx->active_buffer ^= 1;
	ctx->next_packet    = disp_buffer->buffer;
	ClearOTagR(disp_buffer->ot, OT_LENGTH);
}

void *new_primitive(RenderContext *ctx, int z, size_t size)
{
	RenderBuffer *buffer = &(ctx->buffers[ctx->active_buffer]);
	uint8_t      *prim   = ctx->next_packet;

	addPrim(&(buffer->ot[z]), prim);
	ctx->next_packet += size;

	assert(ctx->next_packet <= &(buffer->buffer[BUFFER_LENGTH]));

	return (void *) prim;
}

void draw_text(RenderContext *ctx, int x, int y, int z, const char *text)
{
	RenderBuffer *buffer = &(ctx->buffers[ctx->active_buffer]);

	ctx->next_packet = (uint8_t *)
		FntSort(&(buffer->ot[z]), ctx->next_packet, x, y, text);

	assert(ctx->next_packet <= &(buffer->buffer[BUFFER_LENGTH]));
}

void DrawGrid(RenderContext *ctx, int off_x, int off_y)
{
	for (int x = 0; x < GRID_W; x++)
	{
		for (int y = 0; y < GRID_H; y++)
		{
			TILE *tile = (TILE *) new_primitive(ctx, 1, sizeof(TILE));
			setTile(tile);
			setXY0(tile, off_x+(x*18), off_y+(y*18));
			setWH(tile, 16, 16);
			if(board[x][y].mine) setRGB0(tile, 0, 0, 0);
			else switch(board[x][y].neighbors)
			{
				case 1: setRGB0(tile, 0, 0, 255); break;
				case 2: setRGB0(tile, 0, 255, 0); break;
				case 3: setRGB0(tile, 255, 0, 0); break;
				case 4: setRGB0(tile, 0, 0, 127); break;
				case 5: setRGB0(tile, 127, 127, 0); break;
				case 6: setRGB0(tile, 0, 127, 127); break;
				case 7: setRGB0(tile, 25, 25, 25); break;
				case 8: setRGB0(tile, 127, 127, 127); break;
				default: setRGB0(tile, 255, 255, 255); break;
			}
		}
	}
}

/* Main */

#define SCREEN_XRES 320
#define SCREEN_YRES 240

int main(int argc, const char **argv)
{
	Pad_Init();
	
	ResetRCnt(RCntCNT2);
	SetRCnt(RCntCNT2, 0xffff, RCntMdNOINTR);
	StartRCnt(RCntCNT2);
	
	ResetGraph(0);
	FntLoad(960, 0);

	RenderContext ctx;
	setup_context(&ctx, SCREEN_XRES, SCREEN_YRES, 75, 75, 75);
	
	int loaded = 0;
	
	while (loaded==0)
	{
		draw_text(&ctx, 6, 12, 0, "MineSweeperPSX v0.1      -By-AcidNT3.1-");
		draw_text(&ctx, 6, 36, 0, "PRESS START");
		
		Pad_Update();
		uint16_t p = Pad_Pressed();

		if (p & PAD_START) loaded = 1;
		flip_buffers(&ctx);
	}
	
	int seed = GetRCnt(RCntCNT2);
	srand(seed);
	
	GenerateBoard();
	CalculateNumbers();

	for (;;)
	{
		DrawGrid(&ctx, 6, 24);
		char game_str[64];
		sprintf(game_str, "MINES: %d", mine_count);
		draw_text(&ctx, 6, 12, 0, game_str);
		
		char str[64];
		sprintf(str, "Seed: %d", seed);
		draw_text(&ctx, 6, 220, 0, str);
		
		Pad_Update();
		uint16_t p = Pad_Pressed();

		if (p & PAD_LEFT) cursorX--;
		if (p & PAD_RIGHT) cursorX++;
		if (p & PAD_UP) cursorY--;
		if (p & PAD_DOWN) cursorY++;
		
		flip_buffers(&ctx);
	}

	return 0;
}