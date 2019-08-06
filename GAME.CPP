#include <graphics.h>
#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include <math.h>
#include <malloc.h>
#include <memory.h>
#include <string.h>
#include <fstream.h>
#include <dos.h>
#include "hi.hpp"


// keyboard stuff

#define KEYBOARD_INT    0x09         // the keyboard interrupt vector
#define KEY_BUFFER      0x60         // keyboard buffer area
#define KEY_CONTROL     0x61         // keyboard control register
#define INT_CONTROL     0x20         // interrupt control register

// make and break codes for the arrow keys

#define MAKE_RIGHT      77
#define MAKE_LEFT       75
#define MAKE_UP         72
#define MAKE_DOWN       80

#define BREAK_RIGHT     205
#define BREAK_LEFT      203
#define BREAK_UP        200
#define BREAK_DOWN      208

// indices into arrow key state table

#define INDEX_UP        0
#define INDEX_DOWN      1
#define INDEX_RIGHT     2
#define INDEX_LEFT      3

#define XCELLS 4
#define YCELLS 4
#define WORLDXMAX 959
#define WORLDYMAX 599

#define XMIDPOINT 159
#define YMIDPOINT 99

#define VGA_WIDTH  (unsigned int)320
#define VGA_HEIGHT (unsigned int)200

#define SCREEN_WIDTH      320
#define SCREEN_HEIGHT     200

#define VGA_INPUT_STATUS_1    0x3DA
#define VGA_VSYNC_MASK        0x08


#define PALETTE_MASK         0x3c6
#define PALETTE_REGISTER_RD  0x3c7
#define PALETTE_REGISTER_WR  0x3c8
#define PALETTE_DATA         0x3c9


typedef struct RGBcolor
  {
    unsigned char red;
    unsigned char green;
    unsigned char blue;
  };





enum bool {FALSE = 0, TRUE = 1};

int raw_key = 0;                  // the global raw keyboard data aquired from the ISR

int key_table[4] = {0,0,0,0}; // the key state table for the motion keys

void interrupt (*OldKeyIsr)(...); // holds old keyboard interrupt handler


unsigned char far *videoBuffer = (char far *)0xA0000000L;
unsigned char *doubleBuffer = NULL;

unsigned char *buffer0 = NULL;
unsigned char *buffer1 = NULL;
unsigned char *buffer2 = NULL;
unsigned char *buffer3 = NULL;

RGBcolor palette[256];

unsigned int worldMap[XCELLS][YCELLS];

int xframepos = 0;
int yframepos = 0;


bool leftscrollstopped = FALSE;
bool rightscrollstopped = FALSE;
bool upscrollstopped = FALSE;
bool downscrollstopped = FALSE;




void initBuffers()
{
  doubleBuffer = (char *)farmalloc(VGA_WIDTH * VGA_HEIGHT);
  memset(doubleBuffer, 0, VGA_WIDTH * VGA_HEIGHT);

  buffer0 = (char *)farmalloc(VGA_WIDTH * VGA_HEIGHT);
  memset(buffer0, 0, VGA_WIDTH * VGA_HEIGHT);
  buffer1 = (char *)farmalloc(VGA_WIDTH * VGA_HEIGHT);
  memset(buffer1, 0, VGA_WIDTH * VGA_HEIGHT);
  buffer2 = (char *)farmalloc(VGA_WIDTH * VGA_HEIGHT);
  memset(buffer2, 0, VGA_WIDTH * VGA_HEIGHT);
  buffer3 = (char *)farmalloc(VGA_WIDTH * VGA_HEIGHT);
  memset(buffer3, 0, VGA_WIDTH * VGA_HEIGHT);

  for (int i = 0; i < XCELLS; i++)
    for (int j = 0; j < YCELLS; j++)
      {
	worldMap[i][j] = himalloc(VGA_WIDTH * VGA_HEIGHT);
	if (worldMap[i][j] == 0)
	  {
	    printf("Not enough himem.\n");
	    exit(1);
	  }
      }

}





void showDoubleBuffer(char far *buffer)
{
  asm push DS
  asm les DI, videoBuffer
  asm lds SI, buffer
  asm mov CX, 320*200/2
  asm cld
  asm rep movsw
  asm pop DS
}






inline void plotPixelFast(int x, int y, unsigned char color)
{
  doubleBuffer[((y<<8) + (y<<6)) + x] = color;
}




inline void drawHLine(int X1, int X2, int Y, unsigned char color)
{
  memset((doubleBuffer + (Y << 8) + (Y << 6) + X1), color, (X2 - X1 + 1));
}


void SetPaletteRegister(int index, RGBcolor color)
{
  outp(PALETTE_MASK, 0xff);
  outp(PALETTE_REGISTER_WR, index);
  outp(PALETTE_DATA, color.red);
  outp(PALETTE_DATA, color.green);
  outp(PALETTE_DATA, color.blue);
}





void PCXLoad(char *filename, char* buffer, RGBcolor* palette)
{
  FILE *fp = fopen(filename, "rb");
  unsigned char data;
  int numBytes;

  for (int i = 0; i < 128; i++)
    {
      data = getc(fp);
    }


  unsigned int count = 0;

  while (count < (unsigned int)(SCREEN_WIDTH * SCREEN_HEIGHT))
    {
      data = getc(fp);

      if (data >= 192)
	{
	  numBytes = data - 192;
	  data = getc(fp);
	  for (i = 0; i < numBytes; i++)
	    buffer[count++] = data;
	}
      else
	{
	  buffer[count++] = data;
	}
    }


   fseek(fp, -768L, SEEK_END);
   for (i = 0; i < 256; i++)
     {
       palette[i].red = (getc(fp) >> 2);
       palette[i].green = (getc(fp) >> 2);
       palette[i].blue = (getc(fp) >> 2);
     }

  fclose(fp);

}








class sprite
{
public:
  sprite(char *imagefile, int ixsize, int iysize, int inumimages,
	 int imagesperline, int ixpos, int iypos);
  ~sprite();


  void update();
  void draw();

  int numimages;
  int xsize;
  int ysize;
  int currentimage;

  int speed;
  int direction;

  int xpos;
  int ypos;
  int dx;
  int dy;

  int updatecount;

  unsigned char ***images;
};



sprite::sprite(char *imagefile, int ixsize, int iysize, int inumimages,
	       int imagesperline, int ixpos, int iypos)
{
  numimages = inumimages;
  xsize = ixsize;
  ysize = iysize;
  currentimage = 0;

  xpos = ixpos;
  ypos = iypos;
  dx = 0;
  dy = 0;


  speed = 0;
  direction = 0;

  int yoffset = 0;
  int xoffset = 0;

  PCXLoad(imagefile, buffer0, palette);

  images = new unsigned char**[numimages];

  for (int k = 0; k < numimages; k++)
    {
      images[k] = new unsigned char*[xsize];

      xoffset = (k % imagesperline) * (xsize + 1) + 1;

      if ((k % imagesperline) == 0)
	yoffset = (k / imagesperline) * (ysize + 1) + 1;

      for (int i = 0; i < xsize; i++)
	{
	  images[k][i] = new unsigned char[ysize];

	  for (int j = 0; j < ysize; j++)
	  {
	    images[k][i][j] = buffer0[((j + yoffset) * SCREEN_WIDTH) + i +
				      xoffset];
	    images[k][i][j] += 128;
	  }
	}
    }

  for (int count = 0; count < 128; count++)
    SetPaletteRegister(count + 128, palette[count]);


}




sprite::~sprite()
{
}





void sprite::update()
{
  switch(direction)
    {
      case 0:
	dx = 0;
	dy = -speed;
	break;
      case 1:
	dx = speed / 3;
	dy = -(2 * speed) / 3;
	break;
      case 2:
	dx = speed / 2;
	dy = -speed / 2;
	break;
      case 3:
	dx = (2 * speed) / 3;
	dy = -speed / 3;
	break;

      case 4:
	dx = speed;
	dy = 0;
	break;
      case 5:
	dx = (2 * speed) / 3;
	dy = speed / 3;
	break;
      case 6:
	dx = speed / 2;
	dy = speed / 2;
	break;
      case 7:
	dx = speed / 3;
	dy = (2 * speed) / 3;
	break;

      case 8:
	dx = 0;
	dy = speed;
	break;
      case 9:
	dx = -speed / 3;
	dy = (2 * speed) / 3;
	break;
      case 10:
	dx = -speed / 2;
	dy = speed / 2;
	break;
      case 11:
	dx = -(2 * speed) / 3;
	dy = speed / 3;
	break;

      case 12:
	dx = -speed;
	dy = 0;
	break;
      case 13:
	dx = -(2 * speed) / 3;
	dy = -speed / 3;
	break;
      case 14:
	dx = -speed / 2;
	dy = -speed / 2;
	break;
      case 15:
	dx = -speed / 3;
	dy = -(2 * speed) / 3;
	break;

    }


  if ((!rightscrollstopped) && (!leftscrollstopped))
    xframepos += dx;

  if ((!upscrollstopped) && (!downscrollstopped))
    yframepos += dy;

  if (xframepos < 0)
    {
      xframepos = 0;
      rightscrollstopped = TRUE;
    }

  if (yframepos < 0)
    {
      yframepos = 0;
      downscrollstopped = TRUE;
    }

  if (xframepos > WORLDXMAX)
    {
      xframepos = WORLDXMAX;
      leftscrollstopped = TRUE;
    }

  if (yframepos > WORLDYMAX)
    {
      yframepos = WORLDYMAX;
      upscrollstopped = TRUE;
    }

  xpos += dx;
  ypos += dy;

  if (xpos < 0) xpos = 0;
  if (ypos < 0) ypos = 0;
  if (xpos > WORLDXMAX + 295) xpos = WORLDXMAX + 295;
  if (ypos > WORLDYMAX + 175) ypos = WORLDYMAX + 175;

  if (rightscrollstopped && (xpos >= XMIDPOINT))
    {
      rightscrollstopped = FALSE;
      xpos = XMIDPOINT;
    }

  if (leftscrollstopped && ((xpos - xframepos) <= XMIDPOINT))
    {
      leftscrollstopped = FALSE;
      xpos = xframepos + XMIDPOINT;
    }

  if (downscrollstopped && (ypos >= YMIDPOINT))
    {
      downscrollstopped = FALSE;
      ypos = YMIDPOINT;
    }

  if (upscrollstopped && ((ypos - yframepos) <= YMIDPOINT))
    {
      upscrollstopped = FALSE;
      ypos = yframepos + YMIDPOINT;
    }

}



void sprite::draw()
{
  int xscreenpos = xpos - xframepos;
  int yscreenpos = ypos - yframepos;

  currentimage = direction;

  for (int i = 0; i < xsize; i++)
    for (int j = 0; j < ysize; j++)
      if (images[currentimage][i][j] != 128)
	{
	  plotPixelFast(xscreenpos + i, yscreenpos + j,
			images[currentimage][i][j]);
	}
}













void scroll(char* buffer1, char* buffer2, char* buffer3, char* buffer4,
	    int hdelta, int vdelta)
{
  for (unsigned int i = 0; i < (SCREEN_HEIGHT - vdelta); i++)
    {
      memcpy(doubleBuffer + (i * SCREEN_WIDTH),
	     buffer1 + ((i + vdelta) * SCREEN_WIDTH) + hdelta,
	     SCREEN_WIDTH - hdelta);
      memcpy(doubleBuffer + (i * SCREEN_WIDTH) + (SCREEN_WIDTH - hdelta),
	     buffer2 + ((i + vdelta) * SCREEN_WIDTH),
	     hdelta);
    }
  for (i = 0; i < vdelta; i++)
    {
      memcpy(doubleBuffer + (i + (SCREEN_HEIGHT - vdelta)) * SCREEN_WIDTH,
	     buffer3 + (i * SCREEN_WIDTH) + hdelta,
	     SCREEN_WIDTH - hdelta);
      memcpy(doubleBuffer + ((i + (SCREEN_HEIGHT - vdelta)) * SCREEN_WIDTH)
	     + (SCREEN_WIDTH - hdelta),
	     buffer4 + (i * SCREEN_WIDTH),
	     hdelta);
    }

}


/*void scroll(unsigned int buffer1, unsigned int buffer2, unsigned int buffer3,
	    unsigned int buffer4, int hdelta, int vdelta)
{
  for (unsigned int i = 0; i < (SCREEN_HEIGHT - vdelta); i++)
    {
      hi2real(doubleBuffer + (i * SCREEN_WIDTH),
	     buffer1, ((i + vdelta) * SCREEN_WIDTH) + hdelta,
	     SCREEN_WIDTH - hdelta);
      hi2real(doubleBuffer + (i * SCREEN_WIDTH) + (SCREEN_WIDTH - hdelta),
	     buffer2, ((i + vdelta) * SCREEN_WIDTH),
	     hdelta);
    }
  for (i = 0; i < vdelta; i++)
    {
      hi2real(doubleBuffer + (i + (SCREEN_HEIGHT - vdelta)) * SCREEN_WIDTH,
	     buffer3, (i * SCREEN_WIDTH) + hdelta,
	     SCREEN_WIDTH - hdelta);
      hi2real(doubleBuffer + ((i + (SCREEN_HEIGHT - vdelta)) * SCREEN_WIDTH)
	     + (SCREEN_WIDTH - hdelta),
	     buffer4, (i * SCREEN_WIDTH),
	     hdelta);
    }

}
*/


void updateScreen()
{
  while(!(inp(VGA_INPUT_STATUS_1) & VGA_VSYNC_MASK))
    {
      // retrace is not happening; leave screen alone
    }

  // retrace is happening
  // copy double buffer to video memory

  showDoubleBuffer(doubleBuffer);

  while(inp(VGA_INPUT_STATUS_1) & VGA_VSYNC_MASK)
    {
      // wait for end of retrace
    }
}





void interrupt NewKeyInt(...)
{
   asm sti
   asm in al, KEY_BUFFER
   asm xor ah,ah
   asm mov raw_key, ax
   asm in al, KEY_CONTROL
   asm or al, 82h
   asm out KEY_CONTROL,al
   asm and al,7fh
   asm out KEY_CONTROL,al
   asm mov al,20h
   asm out INT_CONTROL,al

   switch(raw_key)
     {
      case MAKE_UP:    // pressing up
	   {
	   key_table[INDEX_UP]    = 1;
	   } break;

      case MAKE_DOWN:  // pressing down
	   {
	   key_table[INDEX_DOWN]  = 1;
	   } break;

      case MAKE_RIGHT: // pressing right
	   {
	   key_table[INDEX_RIGHT] = 1;
	   } break;

      case MAKE_LEFT:  // pressing left
	   {
	   key_table[INDEX_LEFT]  = 1;
	   } break;

      case BREAK_UP:    // releasing up
	   {
	   key_table[INDEX_UP]    = 0;
	   } break;

      case BREAK_DOWN:  // releasing down
	   {
	   key_table[INDEX_DOWN]  = 0;
	   } break;

      case BREAK_RIGHT: // releasing right
	   {
	   key_table[INDEX_RIGHT] = 0;
	   } break;

      case BREAK_LEFT:  // releasing left
	   {
	   key_table[INDEX_LEFT]  = 0;
	   } break;

      default: break;


      } // end switch

}




int huge detectVGA()
{
   int driver, mode, sugmode = 0;

   detectgraph(&driver, &mode);
   if ((driver == EGA) || (driver == VGA))
      return sugmode;
   else
      return grError;
}




void checkerrors()
{
   int errorcode;

   errorcode = graphresult();
   if (errorcode != grOk)
   {
      printf("Graphics error: %s\n", grapherrormsg(errorcode));
      printf("Press any key to halt:");
      getch();
      exit(1);
   }
}



void main(int argc, char **argv)
{

  if (! pinghi())
    {
      printf("HIMEM not installed.\n");
      exit(1);
    }
  else
    printf("HIMEM installed.\n");

  /* Initialize library */
  hiinit();

  /* Report contiguous and total amount of memory available */
  printf("%lu contiguous bytes available\n", hicontig());
  printf("%lu bytes available\n", himemavl());



//  getch();


  int gdriver, gmode;
  gdriver = installuserdriver("VGA256", detectVGA);
  gdriver = DETECT;
  checkerrors();
  initgraph(&gdriver, &gmode, "");
  checkerrors();



  initBuffers();

  int xPic = 0, yPic = 0, hdelta = 0, vdelta = 0;
  int oldxpic = 0, oldypic = 0;

//  buffer0 = worldMap[xPic][yPic];
//  buffer1 = worldMap[xPic][yPic + 1];
//  buffer2 = worldMap[xPic + 1][yPic];
//  buffer3 = worldMap[xPic + 1][yPic + 1];
  hdelta = 0;
  vdelta = 0;

  sprite *dork = new sprite("cars.pcx", 24, 24, 16, 8, 160, 100);

  PCXLoad("pic00.pcx", buffer0, palette);
  real2hi(worldMap[0][0], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);
  real2hi(worldMap[0][2], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);

  PCXLoad("pic01.pcx", buffer0, palette);
  real2hi(worldMap[0][1], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);
  real2hi(worldMap[0][3], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);

  PCXLoad("pic10.pcx", buffer0, palette);
  real2hi(worldMap[1][0], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);
  real2hi(worldMap[1][2], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);

  PCXLoad("pic11.pcx", buffer0, palette);
  real2hi(worldMap[1][1], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);
  real2hi(worldMap[1][3], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);

  PCXLoad("pic20.pcx", buffer0, palette);
  real2hi(worldMap[2][0], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);
  real2hi(worldMap[2][2], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);

  PCXLoad("pic21.pcx", buffer0, palette);
  real2hi(worldMap[2][1], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);
  real2hi(worldMap[2][3], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);

  real2hi(worldMap[3][0], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);
  real2hi(worldMap[3][1], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);
  real2hi(worldMap[3][2], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);
  real2hi(worldMap[3][3], 0L, buffer0, VGA_WIDTH * VGA_HEIGHT);


  hi2real(buffer0, worldMap[xPic][yPic], 0L, VGA_WIDTH * VGA_HEIGHT);
  hi2real(buffer1, worldMap[xPic + 1][yPic], 0L, VGA_WIDTH * VGA_HEIGHT);
  hi2real(buffer2, worldMap[xPic][yPic + 1], 0L, VGA_WIDTH * VGA_HEIGHT);
  hi2real(buffer3, worldMap[xPic + 1][yPic + 1], 0L, VGA_WIDTH * VGA_HEIGHT);


  for (int count = 0; count < 128; count++)
    SetPaletteRegister(count, palette[count]);




  OldKeyIsr = _dos_getvect(KEYBOARD_INT);
  _dos_setvect(KEYBOARD_INT, NewKeyInt);



  scroll(buffer0, buffer1, buffer2, buffer3, hdelta, vdelta);
//  scroll(worldMap[xPic][yPic], worldMap[xPic + 1][yPic],
//	 worldMap[xPic][yPic + 1], worldMap[xPic + 1][yPic + 1],
//	 hdelta, vdelta);


  dork->update();

  while(raw_key != 1)
    {
      if (key_table[INDEX_RIGHT])
	dork->direction = (dork->direction + 1) % 16;
      if (key_table[INDEX_LEFT])
	{
	  dork->direction = (dork->direction - 1);
	  if (dork->direction < 0)
	    dork->direction += 16;
	}
      if (key_table[INDEX_UP])
	dork->speed += 1;
      if (key_table[INDEX_DOWN])
	dork->speed -= 1;

      if (dork->speed < 0) dork->speed = 0;

      dork->update();   // set new position; decide how much to scroll

      xPic = xframepos / SCREEN_WIDTH;
      yPic = yframepos / SCREEN_HEIGHT;
//      buffer0 = worldMap[xPic][yPic];
//      buffer1 = worldMap[xPic + 1][yPic];
//      buffer2 = worldMap[xPic][yPic + 1];
//      buffer3 = worldMap[xPic + 1][yPic + 1];

      if ((xPic != oldxpic) || (yPic != oldypic))
	{
	  hi2real(buffer0, worldMap[xPic][yPic], 0L, VGA_WIDTH * VGA_HEIGHT);
	  hi2real(buffer1, worldMap[xPic + 1][yPic], 0L, VGA_WIDTH * VGA_HEIGHT);
	  hi2real(buffer2, worldMap[xPic][yPic + 1], 0L, VGA_WIDTH * VGA_HEIGHT);
	  hi2real(buffer3, worldMap[xPic + 1][yPic + 1], 0L, VGA_WIDTH * VGA_HEIGHT);
	}

      oldxpic = xPic;
      oldypic = yPic;

      hdelta = xframepos % SCREEN_WIDTH;
      vdelta = yframepos % SCREEN_HEIGHT;

      scroll(buffer0, buffer1, buffer2, buffer3, hdelta, vdelta);

//      scroll(worldMap[xPic][yPic], worldMap[xPic + 1][yPic],
//	     worldMap[xPic][yPic + 1], worldMap[xPic + 1][yPic + 1],
//	     hdelta, vdelta);

      dork->draw(); // draw sprite

      updateScreen();
    }





  _dos_setvect(KEYBOARD_INT, OldKeyIsr);




  closegraph();
  farfree(doubleBuffer);

  for (int i = 0; i < XCELLS; i++)
    for (int j = 0; j < YCELLS; j++)
      {
	hifree(worldMap[i][j]);
      }
}

