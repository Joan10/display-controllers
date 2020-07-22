//#define DEBUG

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/fb.h>
#include <asm/io.h>
#include <linux/spi/spi.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/time.h>

#define LCD_DC 1
#define BLOCKLEN (4096)
#define BYTES_PER_PIXEL 2
//Quants bytes necessita cada píxel per ser codificat.

//Pins
#define LCD_CS  18                  
#define LCD_CD  22                  
#define LCD_WR  27                  
#define LCD_RD  17                  
#define LCD_RST  4                  
                         
#define IDLE 1              
#define ACTIVE 0

#define TFTWIDTH   320
#define TFTHEIGHT  240

#define RD_ACTIVE  gpio_set_value(lcd_gpio[3].gpio, ACTIVE)
#define RD_IDLE    gpio_set_value(lcd_gpio[3].gpio, IDLE)
#define WR_ACTIVE  gpio_set_value(lcd_gpio[2].gpio, ACTIVE)
#define WR_IDLE    gpio_set_value(lcd_gpio[2].gpio, IDLE)
#define CD_COMMAND gpio_set_value(lcd_gpio[1].gpio, ACTIVE)
#define CD_DATA    gpio_set_value(lcd_gpio[1].gpio, IDLE)
#define CS_ACTIVE  gpio_set_value(lcd_gpio[0].gpio, ACTIVE)
#define CS_IDLE    gpio_set_value(lcd_gpio[0].gpio, IDLE)
#define WR_STROBE { WR_ACTIVE; for (ili9325_bluff=0; ili9325_bluff<5000; ili9325_bluff++); WR_IDLE; }
//L'espera del bucle és important perquè sinó LCD deixa de funcionar. Posar una espera amb ndelay o udelay retrassa massa l'LCD.


int ili9325_bluff=0;
//Variable per implementar el comptador que fa l'espera al WR_STROBE

struct ili9325_page {
/*
Pàgina del framebuffer de l'LCD. Cada pàgina fa referència a un tros de la pantalla, que comença en x,y i la informació
està a buffer per una longitud de len.
*/
	  unsigned short x;
	  unsigned short y;
	  unsigned short *buffer;
	  unsigned short len;
	  int must_update;
};

struct ili9325 {
/*
Estructura principal del ili9325.
*/
	  struct device *dev; //Punter a l'struct device
	  struct spi_device *spidev; //Punter al dispositiu SPI
	  struct fb_info *info; //Punter a l'struct principal del frame buffer.
	  unsigned int pages_count; //Número de pàgines que permet l'LCD
	  struct ili9325_page *pages; //Array de pages.
	  unsigned long pseudo_palette[17]; //Estructura per implementar la funció de color, necessària per fer de consola.
	  int backlight; //Llum (no emprat)
};

static struct gpio lcd_gpio[] = {
/*
Estructura que fa referència als pins GPIO emprats.
*/
	{ LCD_CS, GPIOF_OUT_INIT_HIGH, "GPIO_CS" },
	{ LCD_CD, GPIOF_OUT_INIT_HIGH, "GPIO_CD" },
	{ LCD_WR, GPIOF_OUT_INIT_HIGH, "GPIO_RW" },
	{ LCD_RD, GPIOF_OUT_INIT_HIGH, "GPIO_RD" },
	{ LCD_RST, GPIOF_OUT_INIT_HIGH, "GPIO_RST" },
};


static void ili9325_lcdpixelxy(struct ili9325 *item, int x, int y);

static void ili9325_write8_spi(struct ili9325 *item, unsigned char value) {
	/*
	 * Enviam un byte per SPI i feim un strobe per escriure. (WR alt, WR baix)
	 */
	uint8_t buf;
	buf = value;
	
	spi_write(item->spidev, &buf, 1);
	WR_STROBE; 

}




static void ili9325_writeRegister16(struct ili9325 *item, uint16_t c, uint16_t d) {
	/*Enviam dos enters de 16 bits per SPI, el primer en mode comanda i el segon en mode dades.
	 * Ideal per escriure a registres interns de la pantalla.
	 */
	uint8_t lo;
	uint8_t hi;
	
	lo = c & 0x00ff;
	hi = (c >> 8) & 0x00ff;
	
	CD_COMMAND;

	ili9325_write8_spi(item,hi);
	ili9325_write8_spi(item,lo);

	
	
	lo = d & 0x00ff;
	hi = (d >> 8) & 0x00ff ;
	
	CD_DATA;
	
	ili9325_write8_spi(item,hi);
	ili9325_write8_spi(item,lo);
}


//static u8 ili9325_spiblock[BLOCKLEN*2];

static int ili9325_spi_write_datablock(struct ili9325 *item, unsigned short *block, int len)
{
	int x;
	unsigned short value;
	uint8_t  lo,hi;
	unsigned short value_ant = 0x0000;
	//ToDo: send in parts if needed
	if (len>BLOCKLEN) {
		dev_err(item->dev, "%s: len > blocklen (%i > %i)\n",__func__, len, BLOCKLEN);
		len=BLOCKLEN;
	}
	
	CS_ACTIVE;
	CD_COMMAND;
	ili9325_write8_spi(item, 0x00);
	ili9325_write8_spi(item, 0x22);
	CD_DATA;
	
	for (x=0; x<len; x++) {
		//x va de dos en dos
		value=block[x];
		if ((value == value_ant) && (x != 0)){
			WR_STROBE;
			WR_STROBE;
		}else{
			
			lo = value & 0x00ff;
			hi = (value >> 8) & 0x00ff;
		
			ili9325_write8_spi(item, hi);
			ili9325_write8_spi(item, lo);
			
		}
		value_ant=value;
		
	}
	CS_IDLE;
	
	return 0;
}



static void ili9325_copy(struct ili9325 *item, unsigned int index)
{ //Posa el contingut de la posició index de la memòria al bus.
	unsigned short x;
	unsigned short y;
	unsigned short *buffer;
	unsigned int len;

	x = item->pages[index].x;
	y = item->pages[index].y;
	buffer = item->pages[index].buffer;
	len = item->pages[index].len;
	/*dev_dbg(item->dev,
	"%s: page[%u]: x=%3hu y=%3hu buffer=0x%p len=%3hu\n",
	__func__, index, x, y, buffer, len);
*/
	CS_ACTIVE;
	/*Passam l'adreça on volem escriure*/
	ili9325_lcdpixelxy(item,x,y);
	ili9325_spi_write_datablock(item, buffer, len);
	CS_IDLE;
}


static void ili9325_update_all(struct ili9325 *item)
{
	unsigned short i;
	struct fb_deferred_io *fbdefio = item->info->fbdefio;
	for (i = 0; i < item->pages_count; i++) {
		item->pages[i].must_update=1;
	}
	schedule_delayed_work(&item->info->deferred_work, fbdefio->delay);


/* schedule_delayed_work(work, delay) — put work task in global workqueue after delay	 
 * work: job to be done 
 * delay: number of jiffies to wait
 *  
 * fbdefio: estructura fb_deferred_io amb un delay
 * &item->info->deferred_work: apunta a l'estructura ili9325, i no sé que fa. Supos q actualitza. 
 * 
*/

}

static void ili9325_update(struct fb_info *info, struct list_head *pagelist)
{
	struct ili9325 *item = (struct ili9325 *)info->par;
	struct page *page;
	int i;
	
//	struct timespec {
//		time_t  tv_sec;     /* seconds */
//		long    tv_nsec;    /* nanoseconds */
//	};
	
	
 // struct timeval start, end;
	

  //do_gettimeofday(&start);
	
	//printk("Soc a ili9325_update :D");
	//We can be called because of pagefaults (mmap'ed framebuffer, pages
	//returned in *pagelist) or because of kernel activity
	//(pages[i]/must_update!=0). Add the former to the list of the latter.
	
	
	
	list_for_each_entry(page, pagelist, lru) {
		item->pages[page->index].must_update=1;
	}
	//dev_dbg(item->dev,"PAGE update %i", j);
	//Copy changed pages.
	
	for (i=0; i<item->pages_count; i++) {
	//ToDo: Small race here between checking and setting must_update,
	//maybe lock?
		if (item->pages[i].must_update) {
			//dev_dbg(item->dev,"JAGHDJAHDJAHDJ %i\n",i);
			//dev_dbg(item->dev,"2-PAGE page[%i] MUST UPDATE\n",i);
			item->pages[i].must_update=0;
	 		ili9325_copy(item, i);
		}
	}
	
 // do_gettimeofday(&end);
//	dev_dbg(item->dev,"s: %ld u: %ld m: %ld f1:%ld f2:%ld\n", end.tv_sec-start.tv_sec, end.tv_usec-start.tv_usec, ((end.tv_sec * 1000000 + end.tv_usec)
//		  - (start.tv_sec * 1000000 + start.tv_usec)), end.tv_sec * 1000000 + end.tv_usec,start.tv_sec * 1000000 + start.tv_usec );
	
	
}


#define ILI932X_START_OSC          0x00
#define ILI932X_DRIV_OUT_CTRL      0x01
#define ILI932X_DRIV_WAV_CTRL      0x02
#define ILI932X_ENTRY_MOD          0x03
#define ILI932X_RESIZE_CTRL        0x04
#define ILI932X_DISP_CTRL1         0x07
#define ILI932X_DISP_CTRL2         0x08
#define ILI932X_DISP_CTRL3         0x09
#define ILI932X_DISP_CTRL4         0x0A
#define ILI932X_RGB_DISP_IF_CTRL1  0x0C
#define ILI932X_FRM_MARKER_POS     0x0D
#define ILI932X_RGB_DISP_IF_CTRL2  0x0F
#define ILI932X_POW_CTRL1          0x10
#define ILI932X_POW_CTRL2          0x11
#define ILI932X_POW_CTRL3          0x12
#define ILI932X_POW_CTRL4          0x13
#define ILI932X_GRAM_HOR_AD        0x20
#define ILI932X_GRAM_VER_AD        0x21
#define ILI932X_RW_GRAM            0x22
#define ILI932X_POW_CTRL7          0x29
#define ILI932X_FRM_RATE_COL_CTRL  0x2B
#define ILI932X_GAMMA_CTRL1        0x30
#define ILI932X_GAMMA_CTRL2        0x31
#define ILI932X_GAMMA_CTRL3        0x32
#define ILI932X_GAMMA_CTRL4        0x35
#define ILI932X_GAMMA_CTRL5        0x36
#define ILI932X_GAMMA_CTRL6        0x37
#define ILI932X_GAMMA_CTRL7        0x38
#define ILI932X_GAMMA_CTRL8        0x39
#define ILI932X_GAMMA_CTRL9        0x3C
#define ILI932X_GAMMA_CTRL10       0x3D
#define ILI932X_HOR_START_AD       0x50
#define ILI932X_HOR_END_AD         0x51
#define ILI932X_VER_START_AD       0x52
#define ILI932X_VER_END_AD         0x53
#define ILI932X_GATE_SCAN_CTRL1    0x60
#define ILI932X_GATE_SCAN_CTRL2    0x61
#define ILI932X_GATE_SCAN_CTRL3    0x6A
#define ILI932X_PART_IMG1_DISP_POS 0x80
#define ILI932X_PART_IMG1_START_AD 0x81
#define ILI932X_PART_IMG1_END_AD   0x82
#define ILI932X_PART_IMG2_DISP_POS 0x83
#define ILI932X_PART_IMG2_START_AD 0x84
#define ILI932X_PART_IMG2_END_AD   0x85
#define ILI932X_PANEL_IF_CTRL1     0x90
#define ILI932X_PANEL_IF_CTRL2     0x92
#define ILI932X_PANEL_IF_CTRL3     0x93
#define ILI932X_PANEL_IF_CTRL4     0x95
#define ILI932X_PANEL_IF_CTRL5     0x97
#define ILI932X_PANEL_IF_CTRL6     0x98

#define HX8347G_COLADDRSTART_HI    0x02
#define HX8347G_COLADDRSTART_LO    0x03
#define HX8347G_COLADDREND_HI      0x04
#define HX8347G_COLADDREND_LO      0x05
#define HX8347G_ROWADDRSTART_HI    0x06
#define HX8347G_ROWADDRSTART_LO    0x07
#define HX8347G_ROWADDREND_HI      0x08
#define HX8347G_ROWADDREND_LO      0x09
#define HX8347G_MEMACCESS          0x16
#define TFTLCD_DELAY 0xFF


static const uint16_t ILI932x_regValues[] = {
  ILI932X_START_OSC        , 0x0001, // Start oscillator
  TFTLCD_DELAY             , 50,     // 50 millisecond delay
  ILI932X_DRIV_OUT_CTRL    , 0x0100,
  ILI932X_DRIV_WAV_CTRL    , 0x0700,
  ILI932X_ENTRY_MOD        , 0x1028,
  ILI932X_RESIZE_CTRL      , 0x0000,
  ILI932X_DISP_CTRL2       , 0x0202,
  ILI932X_DISP_CTRL3       , 0x0000,
  ILI932X_DISP_CTRL4       , 0x0000,
  ILI932X_RGB_DISP_IF_CTRL1, 0x0,
  ILI932X_FRM_MARKER_POS   , 0x0,
  ILI932X_RGB_DISP_IF_CTRL2, 0x0,
  ILI932X_POW_CTRL1        , 0x0000,
  ILI932X_POW_CTRL2        , 0x0007,
  ILI932X_POW_CTRL3        , 0x0000,
  ILI932X_POW_CTRL4        , 0x0000,
  TFTLCD_DELAY             , 200,
  ILI932X_POW_CTRL1        , 0x1690,
  ILI932X_POW_CTRL2        , 0x0227,
  TFTLCD_DELAY             , 50,
  ILI932X_POW_CTRL3        , 0x001A,
  TFTLCD_DELAY             , 50,
  ILI932X_POW_CTRL4        , 0x1800,
  ILI932X_POW_CTRL7        , 0x002A,
  TFTLCD_DELAY             , 50,
  ILI932X_GAMMA_CTRL1      , 0x0000,
  ILI932X_GAMMA_CTRL2      , 0x0000,
  ILI932X_GAMMA_CTRL3      , 0x0000,
  ILI932X_GAMMA_CTRL4      , 0x0206,
  ILI932X_GAMMA_CTRL5      , 0x0808,
  ILI932X_GAMMA_CTRL6      , 0x0007,
  ILI932X_GAMMA_CTRL7      , 0x0201,
  ILI932X_GAMMA_CTRL8      , 0x0000,
  ILI932X_GAMMA_CTRL9      , 0x0000,
  ILI932X_GAMMA_CTRL10     , 0x0000,
  ILI932X_GRAM_HOR_AD      , 0x0000,
  ILI932X_GRAM_VER_AD      , 0x0000,
  ILI932X_HOR_START_AD     , 0x0000,
  ILI932X_HOR_END_AD       , 0x00EF,
  ILI932X_VER_START_AD     , 0X0000,
  ILI932X_VER_END_AD       , 0x013F,
  ILI932X_GATE_SCAN_CTRL1  , 0xA700, // Driver Output Control (R60h)
  ILI932X_GATE_SCAN_CTRL2  , 0x0003, // Driver Output Control (R61h)
  ILI932X_GATE_SCAN_CTRL3  , 0x0000, // Driver Output Control (R62h)
  ILI932X_PANEL_IF_CTRL1   , 0X0010, // Panel Interface Control 1 (R90h)
  ILI932X_PANEL_IF_CTRL2   , 0X0000,
  ILI932X_PANEL_IF_CTRL3   , 0X0003,
  ILI932X_PANEL_IF_CTRL4   , 0X1100,
  ILI932X_PANEL_IF_CTRL5   , 0X0000,
  ILI932X_PANEL_IF_CTRL6   , 0X0000,
  ILI932X_DISP_CTRL1       , 0x0133, // Main screen turn on
};


static void __init ili9325_setAddrWindow(struct ili9325 *item, int x1, int y1, int x2, int y2) {
	/*
	 * Seleccionam el trosset de pantalla que agafarem per escriure i col·locam el cursor.
	 * Cal tenir en compte que nosaltres empram la pantalla "girada", és a dir, els els punts que
	 * passarem els hem de rotar.
	 */
	int hst,hen,vst,ven;
	
	hst = TFTHEIGHT-y2-1;
	vst = x1;
	hen = TFTHEIGHT-y1-1;
	ven = x2;
	
	CS_ACTIVE;
	/*	
	ili9325_writeRegister16(item,0x0050, ili9325_abs(y1-240)); // Set address window
	ili9325_writeRegister16(item,0x0051, ili9325_abs(y2-240));
	ili9325_writeRegister16(item,0x0052, x1);
	ili9325_writeRegister16(item,0x0053, x2);
	*/
	
	ili9325_writeRegister16(item,0x0050, hst); // Set address window
	ili9325_writeRegister16(item,0x0051, hen);
	ili9325_writeRegister16(item,0x0052, vst);
	ili9325_writeRegister16(item,0x0053, ven);
	
	/*
	ili9325_writeRegister16(item,0x0050, 0); // Set address window
	ili9325_writeRegister16(item,0x0051, 240);
	ili9325_writeRegister16(item,0x0052, 0);
	ili9325_writeRegister16(item,0x0053, 320);
	*/
	ili9325_writeRegister16(item,0x0020, hen ); // Set address counter to top left
	ili9325_writeRegister16(item,0x0021, vst );
	CS_IDLE;
}


static void ili9325_lcdpixelxy(struct ili9325 *item, int x, int y)
{
	//Movem el cursor a la posició x,y (en posició horitzontal).
	CS_ACTIVE;
	ili9325_writeRegister16(item,0x0020, TFTHEIGHT-y-1); // Set address counter to top left
	ili9325_writeRegister16(item,0x0021, x );
	CS_IDLE;
}



static void __init ili9325_reset(struct ili9325 *item)
{
/*
 * Reset de l'LCD
 */	
	uint8_t i;char buf;
	dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);
	

	/* Inicialització de l'LCD. 
	 * El ili9325_reg_set posa als pins gpio els valors requerits. (item, port sortida, valor)
	 */
	dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);

	
	CS_IDLE;
	CD_DATA;
	WR_IDLE;
	RD_IDLE;
	
	gpio_set_value(lcd_gpio[4].gpio, 0); //Activam reset
	mdelay(2);
	gpio_set_value(lcd_gpio[4].gpio, 1);

	CS_ACTIVE;
	CD_DATA;
	
	ili9325_write8_spi(item, 0x00);
	for(i=0; i<7; i++) WR_STROBE;
	CS_IDLE;
	mdelay(100);
	
}


static void __init ili9325_setup(struct ili9325 *item)
{
/* Inicialització de l'LCD. 
 * El ili9325_reg_set posa als pins gpio els valors requerits. (item, port sortida, valor)
 */

	uint8_t i = 0;
	uint16_t a, d;

	ili9325_reset(item);
	
	
	CS_ACTIVE;
	
	
	dev_dbg(item->dev, "%s: item=0x%p\n", "INIT_LCD", (void *)item);

	while(i < sizeof(ILI932x_regValues) / sizeof(uint16_t)) {
		a = ILI932x_regValues[i++];
		d = ILI932x_regValues[i++];
		if(a == TFTLCD_DELAY) mdelay(d);
		else                  ili9325_writeRegister16(item, a, d);
	}
	CS_IDLE;
	//setRotation(rotation);
	ili9325_setAddrWindow(item,0, 0,  TFTWIDTH-1, TFTHEIGHT-1);
	/* //Alguns tests...:
	ili9325_inunda(item,0xff);
	
	ili9325_setAddrWindow(item,0, 0,  TFTWIDTH-1, TFTHEIGHT-1);
	
	CD_COMMAND;
	CS_ACTIVE;
	ili9325_write8_spi(item, 0x00);
	ili9325_write8_spi(item, 0x22);
	CD_DATA;
	ili9325_write8_spi(item, 0xcc);
	ili9325_write8_spi(item, 0xcc);
	
	for (j=0; j<1000; j++) {
		WR_STROBE;
		WR_STROBE;
		udelay(50);
	}
	
	ili9325_setAddrWindow(item,0, 0,  TFTWIDTH-1, TFTHEIGHT-1);
	
	mdelay(1000);*/

}






static int __init ili9325_video_alloc(struct ili9325 *item)
{
/*
This routine will allocate the buffer for the complete framebuffer. This
is one continuous chunk of 16-bit pixel values; userspace programs
will write here.
*/
	unsigned int frame_size;
	unsigned long length;

	//dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);

	frame_size = item->info->fix.line_length * item->info->var.yres;
	//Calcul de la mida de la pantalla (frame)
	//dev_dbg(item->dev, "%s: item=0x%p frame_size=%u\n",__func__, (void *)item, frame_size);

	item->pages_count = frame_size / PAGE_SIZE;
	//Calculam el número de pàgines agafant la mida de la pantalla entre la mida de les pàgines.
	//Com que la mida de les pàgines és gegant, segurament surti un valor ~0
 
	if ((item->pages_count * PAGE_SIZE) < frame_size) {
	//En cas que la mida necessària total no sigui exacta sumarem un al num de pàgines
		item->pages_count++;
	}
	//dev_dbg(item->dev, "%s: item=0x%p pages_count=%u\n",	__func__, (void *)item, item->pages_count);

	item->info->fix.smem_len = item->pages_count * PAGE_SIZE;
	//Calculam la longitud total del framebuffer. Ho trobam multiplicant la mida de la pàgina pel numero de pagines.
	//Fixi¡s que no agafa el frame_size perque aquest valor no seria exacte i reservaríem menys espai.
	item->info->fix.smem_start = (unsigned long)vmalloc(item->info->fix.smem_len);
	//Reservam l'espai amb el malloc i en treim la direcció
	if (!item->info->fix.smem_start) {
		dev_err(item->dev, "%s: unable to vmalloc\n", __func__);
		return -ENOMEM;
	}
	memset((void *)item->info->fix.smem_start, 0, item->info->fix.smem_len);
	//Omplim la memòria de zeros

	length = (unsigned long ) item->info->fix.smem_len;
	//dev_dbg(item->dev, "smem_start: %p length: %i smem_end: %p", (unsigned long *) item->info->fix.smem_start, item->info->fix.smem_len  , ((unsigned long *) item->info->fix.smem_start)+length );


	return 0;
}

static void ili9325_video_free(struct ili9325 *item)
{
/*
Allibera la memòria del framebuffer
*/
	  dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);
	  vfree((void *)item->info->fix.smem_start);
}



static int __init ili9325_pages_alloc(struct ili9325 *item)
/*
This routine will allocate a ili9325_page struct for each vm page in the
main framebuffer memory. Each struct will contain a pointer to the page
start, an x- and y-offset, and the length of the pagebuffer which is in the framebuffer.

A l'anterior rutina ili9325_video_alloc() reservavem espai per l'usuari, pel framebuffer.
Ara reservam espai per l'estructura de les pàgines.

*/
{
	unsigned short pixels_per_page;
	unsigned short yoffset_per_page;
	unsigned short xoffset_per_page;
	unsigned short index;
	unsigned short x = 0;
	unsigned short y = 0;
	unsigned short *buffer;
	unsigned int len;

	//dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);


	item->pages = kmalloc(item->pages_count * sizeof(struct ili9325_page),GFP_KERNEL);
	//Reservam l'espai que necessitarem a memòria virtual pels structs de les pàgines. 
	//Multiplicam, per això, el num de pagines per la mida de cada pàgina.
	if (!item->pages) {
		dev_err(item->dev, "%s: unable to kmalloc for ili9325_page\n",
			   __func__);
		return -ENOMEM;
	}


//	bytes_per_pixel=(item->info->var.bits_per_pixel / 8);
	//if (bytes_per_pixel == 0) bytes_per_pixel = 1;
	//Si no ho feim així donava errors al kernel de divisó per 0.

	pixels_per_page = PAGE_SIZE / (item->info->var.bits_per_pixel / 8);
	/*
		PAGE_SIZE(Bytes) / bits_per_pixel(Bits) / 8 (bits / Byte) 
		Píxels per cada pàgina. Si la pantalla ocupa dues pàgines tendrem pantalla/2 píxels 
	*/

	yoffset_per_page = pixels_per_page / item->info->var.xres;
	xoffset_per_page = pixels_per_page - (yoffset_per_page * item->info->var.xres);
	/*
		Aquests OFFSETS són la quantitat que augmenten la x i la y a cada pàgina.
		Recordem que cada pàgina fa referència a un tros de la pantalla i aquest comença a x,y.
	*/

	//dev_dbg(item->dev, "%s: item=0x%p pixels_per_page=%hu "		"yoffset_per_page=%hu xoffset_per_page=%hu\n",		__func__, (void *)item, pixels_per_page,		yoffset_per_page, xoffset_per_page);

	buffer = (unsigned short *)item->info->fix.smem_start;
	/*
		El buffer apuntarà a les posicions de memòria reservades a la rutina anterior ili9325_video_alloc
		i contindrà la informació de l'usuari. S'incrementerà en un valor pixels_per_page que apuntarà
		a la propera pàgina.
	*/
	for (index = 0; index < item->pages_count; index++) {
	/*
		Per totes les estructures pàgina calcularem:
			1. La longitud (len): Serà min(len0, pixels_per_page)
			on len0 és la mida total de la pantalla menys tot el que duim darrere.
			len serà casi sempre pixels_per_page, excepte en la darrera pàgina,
			que gràcies al substraend serà només el trosset que calgui.

			2. x i y: Les posicions físiques dels píxels sobre la pantalla. Noti's
			el cas en què ens sobrepassem i hàgim de saltar a la següent fila, que haurem
			de resetejar les x.
	
			3. buffer: Punter a la posició de memòria on es troba la pàgina corresponent.
	*/
		//dev_dbg(item->dev,"xres: %i yres: %i pixels_per_page: %i", item->info->var.xres , item->info->var.yres, pixels_per_page);

		len = (item->info->var.xres * item->info->var.yres) -   (index * pixels_per_page);
		if (len > pixels_per_page) {
			   len = pixels_per_page;
		}

		//dev_dbg(item->dev,"%s: page[%d]: x=%3hu y=%3hu buffer=0x%p len=%3hu\n",__func__, index, x, y, buffer, len);

		item->pages[index].x = x;
		item->pages[index].y = y;
		item->pages[index].buffer = buffer;
		item->pages[index].len = len;

		x += xoffset_per_page;
		if (x >= item->info->var.xres) {
			   y++;
			   x -= item->info->var.xres;
		}
		y += yoffset_per_page;
		buffer += pixels_per_page;
	}
	
		  return 0;
}


static void ili9325_pages_free(struct ili9325 *item)
{
/*
	Buidam la memòria de les pàgines
*/
	dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);
	kfree(item->pages);
}

static inline __u32 CNVT_TOHW(__u32 val, __u32 width)
{
	  return ((val<<width) + 0x7FFF - val)>>16;
}

//This routine is needed because the console driver won't work without it.
static int ili9325_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue, unsigned transp, struct fb_info *info)
{
	
/*
	Rutina que prepara els colors. És necessària per la consola! 
	Posa valors al camp de l'estructura pseudopalette, que defineix els valors
	del color de la consola.

	Crec que a la memòria es guarden els colors en funció de "value", és a dir, del
	que es calculi d'aquí.
*/
	int ret = 1;
	//printk("%s: regno: %u rgbt: %u %u %u %u\n", __func__, regno, red, green, blue, transp );
	/*
	* If greyscale is true, then we convert the RGB value
	* to greyscale no matter what visual we are using.
	*/
	if (info->var.grayscale)
		red = green = blue = (19595 * red + 38470 * green +
						  7471 * blue) >> 16;
	switch (info->fix.visual) {
	case FB_VISUAL_TRUECOLOR:
		if (regno < 16) {
			u32 *pal = info->pseudo_palette;
			u32 value;

			red = CNVT_TOHW(red, info->var.red.length);
			green = CNVT_TOHW(green, info->var.green.length);
			blue = CNVT_TOHW(blue, info->var.blue.length);
			transp = CNVT_TOHW(transp, info->var.transp.length);

			value = (red << info->var.red.offset) |
				 (green << info->var.green.offset) |
				 (blue << info->var.blue.offset) |
				 (transp << info->var.transp.offset);

			pal[regno] = value;
			ret = 0;
		}
		break;
	case FB_VISUAL_STATIC_PSEUDOCOLOR:
	case FB_VISUAL_PSEUDOCOLOR:
		break;
	}
	return ret;
}
	
static int ili9325_blank(int blank_mode, struct fb_info *info)
{
	  struct ili9325 *item = (struct ili9325 *)info->par;
	  if (blank_mode == FB_BLANK_UNBLANK)
			item->backlight=1;
	  else
			item->backlight=0;
	  //Item->backlight won't take effect until the LCD is written to. Force that
	  //by dirty'ing a page.
	  item->pages[0].must_update=1;
	  schedule_delayed_work(&info->deferred_work, 0);
	  return 0;
}

static void ili9325_touch(struct fb_info *info, int x, int y, int w, int h)
{
/*
	Actualitza una regió de la pantalla. 
*/
	struct fb_deferred_io *fbdefio = info->fbdefio;
	struct ili9325 *item = (struct ili9325 *)info->par;
	int i, ystart, yend;
	dev_dbg(item->dev, " Estic a %s x: %i y: %i w: %i h: %i", __func__,x,y,w,h);
	if (fbdefio) {
		//Touch the pages the y-range hits, so the deferred io will update them.
		for (i=0; i<item->pages_count; i++) {
			   ystart=item->pages[i].y;
			   yend=item->pages[i].y+(item->pages[i].len/info->fix.line_length)+1;
//			   dev_dbg(item->dev, " INFO PAG %i : ystart: %i yend: %i; .y: %i .len: %i fix.line_length: %i \n", i, ystart, yend, item->pages[i].y, item->pages[i].len,info->fix.line_length  );
			   if (!((y+h)<ystart || y>yend)) {
					 item->pages[i].must_update=1;
			   }
		}
		

		//Schedule the deferred IO to kick in after a delay.
		schedule_delayed_work(&info->deferred_work, fbdefio->delay);
	}
}


/*

	Les següents funcions són requerides pel framebuffer per poder funcionar.
	Bàsicament, li passam les zones que volem actualitzar i ell escriu
	a l'espai corresponent. Llavors actualitzam la pantalla amb el touch()

*/
static void ili9325_fillrect(struct fb_info *p, const struct fb_fillrect *rect)
{
	sys_fillrect(p, rect);
	ili9325_touch(p, rect->dx, rect->dy, rect->width, rect->height);
}

static void ili9325_imageblit(struct fb_info *p, const struct fb_image *image)
{
	sys_imageblit(p, image);
	ili9325_touch(p, image->dx, image->dy, image->width, image->height);
}

static void ili9325_copyarea(struct fb_info *p, const struct fb_copyarea *area)
{
	sys_copyarea(p, area);
	ili9325_touch(p, area->dx, area->dy, area->width, area->height);
}

static ssize_t ili9325_write(struct fb_info *p, const char __user *buf,
						 size_t count, loff_t *ppos)
{
	ssize_t res;
	res = fb_sys_write(p, buf, count, ppos);
	/*
		És un simple copy_from_user.
	*/

	ili9325_touch(p, 0, 0, p->var.xres, p->var.yres);
	return res;
}

/*
	A continuació venen les estructures de dades del framebuffer.
*/

static struct fb_ops ili9325_fbops = {
/*
	Punters a les funcions a implementar.
*/
	  .owner	   = THIS_MODULE,
	  .fb_read	 = fb_sys_read,
	  .fb_write	= ili9325_write,
	  .fb_fillrect  = ili9325_fillrect,
	  .fb_copyarea  = ili9325_copyarea,
	  .fb_imageblit = ili9325_imageblit,
	  .fb_setcolreg   = ili9325_setcolreg,
	  .fb_blank	  = ili9325_blank,
};
	
static struct fb_fix_screeninfo ili9325_fix __initdata = {
/*
	Dades inicials del ili9325
*/
	.id		= "ili9325",
	.type	   = FB_TYPE_PACKED_PIXELS,
	.visual	 = FB_VISUAL_TRUECOLOR,
	.accel	  = FB_ACCEL_NONE,
	.line_length = TFTWIDTH * BYTES_PER_PIXEL,

};

static struct fb_var_screeninfo ili9325_var __initdata = {
	.xres		 = TFTWIDTH,
	.yres		 = TFTHEIGHT,
	.xres_virtual   = TFTWIDTH,
	.yres_virtual   = TFTHEIGHT,
	.width		= TFTWIDTH,
	.height	    = TFTHEIGHT,
	.bits_per_pixel = 8*BYTES_PER_PIXEL,
	.red		  = {11, 5, 0},
	.green		= {5, 6, 0},
	.blue		 = {0, 5, 0},
	.activate	  = FB_ACTIVATE_NOW,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static struct fb_deferred_io ili9325_defio = {
/*
	Funcions IO del framebuffer
*/
	  .delay		= HZ / 20,
	  .deferred_io    = &ili9325_update,
/*

	Deferred_io és la funció a la que cridarà el kernel amb el framebuffer en haver d'enviar les dades al dispositiu extern(LCD)
	Per tant qualsevol operació d'escriptura cridarà a ili9325_update 

*/
};

static int __devinit ili9325_probe(struct spi_device *dev)
{
/*

	Funció que s'executa quan es detecta un dispositiu. Assignam 
	memòria a les estructures de dades del framebuffer i del dispositiu.
	Haurem de copiar les dades que ens venguin del *dev a la nostra
	estructura de dades

*/
	int ret = 0;
	struct ili9325 *item;
	struct fb_info *info;
	
	ret = gpio_request_array(lcd_gpio, ARRAY_SIZE(lcd_gpio));

	if (ret) {
		dev_err(&dev->dev, "%s: unable to get GPIOs\n", __func__);
		return ret;
	}
	
	
	dev_dbg(&dev->dev, "%s\n", __func__);

	
	item = kzalloc(sizeof(struct ili9325), GFP_KERNEL);
	//Assignam memòria a l'estructura del LCD

	if (!item) {
		dev_err(&dev->dev, "%s: unable to kzalloc for ili9325\n", __func__);
		ret = -ENOMEM;
		goto out;
	}

	item->dev = &dev->dev;
	dev_set_drvdata(&dev->dev, item); 
	item->backlight=1;
	//Assignam un driver al dispositiu
	
	item->spidev=dev;
	item->dev=&dev->dev;
	dev_set_drvdata(&dev->dev, item);
	dev_info(&dev->dev, "spi registered, item=0x%p\n", (void *)item);

	info = framebuffer_alloc(sizeof(struct ili9325), &dev->dev);
	//Reserva lloc al framebuffer

	if (!info) {
		ret = -ENOMEM;
		dev_err(&dev->dev,
			   "%s: unable to framebuffer_alloc\n", __func__);
		goto out_item;
	}

	info->pseudo_palette = &item->pseudo_palette;
	item->info = info;
	info->par = item;
	info->dev = &dev->dev;
	info->fbops = &ili9325_fbops;
	info->flags = FBINFO_FLAG_DEFAULT|FBINFO_VIRTFB;
	info->fix = ili9325_fix;
	info->var = ili9325_var;
	//Copiam informació vària

	ret = ili9325_video_alloc(item);
	//reservam la memòria de video
	if (ret) {
		dev_err(&dev->dev,
			   "%s: unable to ili9325_video_alloc\n", __func__);
		goto out_info;
	}
	info->screen_base = (char __iomem *)item->info->fix.smem_start;

	ret = ili9325_pages_alloc(item);
	//reservam la memòria per les pàgines
	if (ret < 0) {
		dev_err(&dev->dev,
			   "%s: unable to ili9325_pages_init\n", __func__);
		goto out_video;
	}

	info->fbdefio = &ili9325_defio;
	fb_deferred_io_init(info);

	/*
		A la info del framebuffer del nou dispositiu li assignam l'estrcutura de dades del deferred IO
		que crida a la funció posada a n'aquesta estructura quan s'hagi d'escriure físicament al dispositiu. 
	*/

	ret = register_framebuffer(info);
	/*Will create the character device 
	that can be used by userspace application with the generic 
	framebuffer API
	*/
	if (ret < 0) {
		dev_err(&dev->dev,
			   "%s: unable to register_frambuffer\n", __func__);
		goto out_pages;
	}

	dev_dbg(item->dev, "%s: item=0x%p\n", "ENTRAM A SETUP", (void *)item);
	ili9325_setup(item);
	//Funció d'inicialització de l'LCD
	ili9325_update_all(item);

	return ret;

out_pages:
	  ili9325_pages_free(item);
out_video:
	  ili9325_video_free(item);
out_info:
	  framebuffer_release(info);
out_item:
	  kfree(item);
out:
	  return ret;
}
	
static int __devexit ili9325_remove(struct spi_device *dev)
/*
	Funció que neteja el device.
*/
{
	  struct ili9325 *item = dev_get_drvdata(&dev->dev);
	  struct fb_info *info;

	  dev_dbg(&dev->dev, "%s\n", __func__);

	  dev_set_drvdata(&dev->dev, NULL);
	  if (item) {
			info = item->info;
			if (info)
				   unregister_framebuffer(info);
			ili9325_pages_free(item);
			ili9325_video_free(item);
			kfree(item);
			if (info)
				   framebuffer_release(info);
	  }
	  return 0;
}


/*	
	No ens interessen aquestes funcions, no existeixen al lcd
*/
#define ili9325_suspend NULL
#define ili9325_resume NULL

static struct spi_driver spi_ili9325_driver = {
/*
	Assignam les funcions del driver spi
*/
	  .driver = {
			.name   = "spi-ili9325",
			.bus    = &spi_bus_type,
			.owner  = THIS_MODULE,
	  },
	  .probe = ili9325_probe,
	  .remove = ili9325_remove,
	  .suspend = ili9325_suspend,
	  .resume = ili9325_resume,
};



static int __init ili9325_init(void)
/*
	Funció que es crida quan es carrega el mòdul.
	Registra els drivers i carrega els gpios necessaris.
*/
{
	int ret1 = 0;

	//pr_debug("PAGE_SIZE:%lu%s\n",PAGE_SIZE ,__func__);
	

	
	ret1 = spi_register_driver(&spi_ili9325_driver);
	if (ret1) {
		pr_err("%s: unable to platform_driver_register\n", __func__);
		return ret1;
	}
	

	
	
	return ret1;
}
	
static void __exit ili9325_exit(void)
/*
	Funció que es crida quan es treu el mòdul.
	Allibera els drivers i l'espai.
*/
{
	pr_debug("%s\n", __func__);
	gpio_set_value(lcd_gpio[3].gpio, 0);
	spi_unregister_driver(&spi_ili9325_driver);	  
	gpio_free_array(lcd_gpio, ARRAY_SIZE(lcd_gpio));

}

module_init(ili9325_init);
module_exit(ili9325_exit);

MODULE_DESCRIPTION("ili9325 LCD Driver");
MODULE_AUTHOR("Joan Arbona (joanf.arbona@gmail.com)");
MODULE_LICENSE("GPL");
