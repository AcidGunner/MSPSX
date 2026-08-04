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

#include <psxcd.h>

void InitAudio(void)
{
    SpuInit();

    SpuSetCommonCDVolume(0x3fff, 0x3fff);

    CdlATV mix = {128, 0, 128, 0};
    CdMix(&mix);
}

#include "pad.h"

#define OT_LENGTH 16

#define BUFFER_LENGTH 32768

typedef struct
{
	DISPENV disp_env;
	DRAWENV draw_env;

	uint32_t ot[OT_LENGTH];
	uint8_t buffer[BUFFER_LENGTH];
} RenderBuffer;

static uint32_t *tilesData;
static CdlFILE tilesFile;

TIM_IMAGE tiles;
uint16_t tileTPage;
uint16_t tileClut;

void LoadTiles(void)
{
    CdInit();
	
	InitAudio();

    if (!CdSearchFile(&tilesFile, "\\TILES.TIM;1")) return;
    tilesData = malloc(tilesFile.size);
    if (!tilesData) return;
	
    CdControl(CdlSetloc, (uint8_t *)&tilesFile.pos, 0);
    int sectors = (tilesFile.size + 2047) / 2048;
    CdRead(sectors, tilesData, CdlModeSpeed);
    if (CdReadSync(0, 0) < 0)
    {
        free(tilesData);
        return;
    }
	
    GetTimInfo(tilesData, &tiles);

    if (tiles.mode & 0x8) LoadImage(tiles.crect, tiles.caddr);
    LoadImage(tiles.prect, tiles.paddr);
    DrawSync(0);

    tileTPage = getTPage(tiles.mode & 0x3, 0, tiles.prect->x, tiles.prect->y);
    if (tiles.mode & 0x8) tileClut = getClut(tiles.crect->x, tiles.crect->y);
}

#define MAX_W 24
#define MAX_H 16

typedef struct
{
	uint8_t mine;
	uint8_t revealed;
	uint8_t flagged;
	int neighbors;
	bool hasNeighborMines;
	uint8_t active;
	uint8_t processed;
} Cell;
Cell board[MAX_W][MAX_H];

typedef struct
{
    int x;
    int y;
} RevealNode;
RevealNode reveal_queue[MAX_W * MAX_H];

int GRID_W = 16;
int GRID_H = 12;
int MINES = 32;
int mine_count = 0;
int reveal_mode = 0;

int control_disable = 0;
int lost = 0;

int revealed_total = 0;
int valid_total = 0;

int cursorX = 0;
int cursorY = 0;

void GenerateBoard()
{
	int xx, yy;
	int active_count = 0;
    memset(board, 0, sizeof(board));
	
	mine_count = MINES;
	
	for (xx = 0; xx < GRID_W; xx++)
    {
        for (yy = 0; yy < GRID_H; yy++)
        {
            board[xx][yy].active = 1;
        }
    }
	
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

void WinCheck()
{
	revealed_total = 0;
	valid_total = 0;
	int wx, wy;

	for (wx = 0; wx < GRID_W; wx++)
	{
		for (wy = 0; wy < GRID_H; wy++)
		{
			if(board[wx][wy].active && !board[wx][wy].mine)
			{
				valid_total++;
				if(board[wx][wy].revealed) revealed_total++;
			}
		}
	}
	
	if(revealed_total >= valid_total)
	{
		mine_count = 0;
		control_disable = 1;
		
		int fx, fy;
		for (fx = 0; fx < GRID_W; fx++)
		{
			for (fy = 0; fy < GRID_H; fy++)
			{
				if (board[fx][fy].mine) board[fx][fy].flagged = 1;
			}
		}
	}
}

void RevealIterative(int startx, int starty)
{
    int head = 0;
    int tail = 0;

    if(startx < 0 || starty < 0 || startx >= GRID_W || starty >= GRID_H) return;
    if(!board[startx][starty].active) return;
    board[startx][starty].processed = 1;

    reveal_queue[tail].x = startx;
    reveal_queue[tail].y = starty;
    tail++;

    while(head < tail)
    {
        int x = reveal_queue[head].x;
        int y = reveal_queue[head].y;
        head++;

        board[x][y].processed = 0;

        if(board[x][y].revealed || board[x][y].flagged>0) continue;

        board[x][y].revealed = 1;

        if(board[x][y].mine)
        {
			control_disable = 1;
			lost = 1;
            return;
        }

        if(board[x][y].neighbors != 0) continue;
		
		int ox, oy;

        for(ox = -1; ox <= 1; ox++)
        {
            for(oy = -1; oy <= 1; oy++)
            {
                if(ox == 0 && oy == 0) continue;

                int nx = x + ox;
                int ny = y + oy;

                if(nx < 0 || ny < 0 || nx >= GRID_W || ny >= GRID_H) continue;
                if(!board[nx][ny].active) continue;
                if(board[nx][ny].revealed) continue;
                if(board[nx][ny].flagged>0) continue;
                if(board[nx][ny].processed) continue;

                board[nx][ny].processed = 1;

                reveal_queue[tail].x = nx;
                reveal_queue[tail].y = ny;
                tail++;
            }
        }
    }
}

void RevealRecursive(int x,int y)
{
    if(x < 0 || y < 0 || x >= GRID_W || y >= GRID_H) return;
	if (!board[x][y].active) return;
    if(board[x][y].revealed || board[x][y].flagged>0) return;

    board[x][y].revealed = 1;
	
    if(board[x][y].mine)
	{
		control_disable = 1;
		lost = 1;
		return;
	}

    if(board[x][y].neighbors != 0) return;
	
	int ox, oy;
    for(ox=-1;ox<=1;ox++)
    {
        for(oy=-1;oy<=1;oy++)
        {
            if(ox==0 && oy==0) continue;
            RevealRecursive(x+ox,y+oy);
        }
    }
}

void Reveal(int x,int y){if(reveal_mode) RevealRecursive(x, y); else RevealIterative(x, y);}

void Chord(int x, int y)
{
    if(x < 0 || y < 0 || x >= GRID_W || y >= GRID_H) return;
    if(!board[x][y].revealed) return;
	if(board[x][y].neighbors == 0) return;
	
    int required = board[x][y].neighbors;
    int flags = 0;

    int nx, ny;

    for(nx = x - 1; nx <= x + 1; nx++)
    {
        for(ny = y - 1; ny <= y + 1; ny++)
        {
            if(nx >= 0 && ny >= 0 && nx < GRID_W && ny < GRID_H)
			{
				if(board[nx][ny].flagged) flags++;
			}
        }
    }
	
    if(flags == required)
    {
        for(nx = x - 1; nx <= x + 1; nx++)
        {
            for(ny = y - 1; ny <= y + 1; ny++)
            {
                if(nx >= 0 && ny >= 0 && nx < GRID_W && ny < GRID_H)
                {
					if(!board[nx][ny].active) continue;
                    if(board[nx][ny].flagged==0)
                    {
                        if(board[nx][ny].mine)
                        {
							control_disable = 1;
							lost = 1;
                            return;
                        }

                        Reveal(nx, ny);
                    }
                }
            }
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

void DrawSprite(RenderContext *ctx, int x, int y, int tileIndex, int z)
{
    POLY_FT4 *spr = (POLY_FT4 *)new_primitive(ctx, z, sizeof(POLY_FT4));
    setPolyFT4(spr);
	
    setRGB0(spr, 128, 128, 128);
    setXY4(spr, x, y, x + 16, y, x, y + 16, x + 16, y + 16);
    int u = (tileIndex % 20) * 16;
    int v = (tileIndex / 20) * 16;
    setUV4(spr, u, v, u + 16, v, u, v + 16, u + 16, v + 16);

    spr->tpage = tileTPage;
    spr->clut  = tileClut;
}

int off_x = 6;
int off_y = 24;

#define SCREEN_XRES 320
#define SCREEN_YRES 240

void DrawBack(RenderContext *ctx)
{
	static int anim = 0;
	anim++;
	int a = (anim / 30) % 2;
	
	for (int x = -10; x < SCREEN_XRES; x+=16)
	{
		for (int y = 8; y < SCREEN_YRES; y+=16)
		{
			DrawSprite(ctx, x, y, 2+a, 7);
		}
	}
}

void DrawGrid(RenderContext *ctx)
{	
	static int anim = 0;
	anim++;
	int a = (anim / 30) % 2;
	
	for (int x = 0; x < GRID_W; x++)
	{
		for (int y = 0; y < GRID_H; y++)
		{
			int tile = 0;
			int sx = off_x+(x*16);
			int sy = off_y+(y*16);
			
			if(sx<-10 || sy<8 || sx>SCREEN_XRES || sy>SCREEN_YRES) continue;
			
			if((board[x][y].revealed || lost))
			{
				DrawSprite(ctx, sx, sy, 2+a, 6);
				
				if(board[x][y].mine) DrawSprite(ctx, sx+a, sy, 114, 4);
				else if(board[x][y].neighbors>0)
				{
					switch(board[x][y].neighbors)
					{
						case 1: tile = 105; break;
						case 2: tile = 106; break;
						case 3: tile = 107; break;
						case 4: tile = 124; break;
						case 5: tile = 125; break;
						case 6: tile = 126; break;
						case 7: tile = 127; break;
						case 8: tile = 144; break;
					}
					DrawSprite(ctx, sx, sy, tile, 5);
				}
			}
			else DrawSprite(ctx, sx, sy, 0+a, 6);
			
			if(board[x][y].flagged) DrawSprite(ctx, sx, sy, 112+a, 5);
		}
	}
}

int FIRST_CLICK = 0;

void PlayTrack(int track)
{
    uint8_t result[8];
    uint8_t track_bcd = itob(track);

    CdControlB(CdlGetTD, &track_bcd, result);

    CdlLOC loc;
    loc.minute = result[1];
    loc.second = result[2];
    loc.sector = 0;
    loc.track = track;

    CdControlB(CdlSetloc, &loc, result);

    CdControlB(CdlPlay, NULL, result);
}

int main(int argc, const char **argv)
{
	ResetRCnt(RCntCNT2);
	SetRCnt(RCntCNT2, 0xffff, RCntMdNOINTR);
	StartRCnt(RCntCNT2);
	
	ResetGraph(0);
	FntLoad(960, 0);

	RenderContext ctx;
	setup_context(&ctx, SCREEN_XRES, SCREEN_YRES, 0, 0, 155);
	
	LoadTiles();
	Pad_Init();
	
	int loaded = 0;
	control_disable = 0;
	lost = 0;
	
	while (loaded==0)
	{
		DrawBack(&ctx);
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
	FIRST_CLICK = 1;
	
	PlayTrack(2);
	
	for (;;)
	{
		DrawBack(&ctx);
		DrawGrid(&ctx);
		
		char game_str[64];
		sprintf(game_str, "MINES: %d", mine_count);
		draw_text(&ctx, 6, 12, 0, game_str);
		
		Pad_Update();
		uint16_t p = Pad_Pressed();
		
		if(!control_disable)
		{
			int sx = off_x+(cursorX*16);
			int sy = off_y+(cursorY*16);
			DrawSprite(&ctx, sx, sy, 240, 3);
			
			if(p & PAD_LEFT && cursorX>0) cursorX--;
			if(p & PAD_RIGHT && cursorX<GRID_W-1) cursorX++;
			if(p & PAD_UP && cursorY>0) cursorY--;
			if(p & PAD_DOWN && cursorY<GRID_H-1) cursorY++;
			
			if(p & PAD_CROSS)
			{
				//if(FIRST_CLICK) timer_running = 1;
				
				if(board[cursorX][cursorY].mine && FIRST_CLICK)
				{
					do
					{
						GenerateBoard();
						CalculateNumbers();
					}
					while(board[cursorX][cursorY].mine);
				}
				
				if (board[cursorX][cursorY].revealed) Chord(cursorX, cursorY);
				else Reveal(cursorX, cursorY);
				
				FIRST_CLICK = 0;
				WinCheck();
			}
			
			if(p & PAD_CIRCLE)
			{
				if(!board[cursorX][cursorY].revealed)
				{
					if(board[cursorX][cursorY].flagged == 0) mine_count--;
					board[cursorX][cursorY].flagged++;
					
					if(board[cursorX][cursorY].flagged > 1)
					{
						board[cursorX][cursorY].flagged = 0;
						mine_count++;
					}
				}
			}
		}
		else
		{
			if(lost) draw_text(&ctx, 200, 12, 0, "GAME OVER.");
			else draw_text(&ctx, 200, 12, 0, "YOU WIN!");
			
			draw_text(&ctx, 120, 24, 0, "PRESS START TO TRY AGAIN");
			if(p & PAD_START)
			{
				GenerateBoard();
				CalculateNumbers();
				control_disable = 0;
				lost = 0;
				FIRST_CLICK = 1;
				cursorX = 0;
				cursorY = 0;
			}
		}
		
		flip_buffers(&ctx);
	}

	return 0;
}