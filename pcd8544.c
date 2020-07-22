#define DEBUG

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


#define LCD_RESET 0
#define LCD_DC 1
#define BLOCKLEN (4096)
#define BYTES_PER_PIXEL 1
//Quants bytes necessita cada píxel per ser codificat.


struct pcd8544_page {
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

struct pcd8544 {
/*
Estructura principal del PCD8544.
*/
	  struct device *dev; //Punter a l'struct device
	  struct spi_device *spidev; //Punter al dispositiu SPI
	  struct fb_info *info; //Punter a l'struct principal del frame buffer.
	  unsigned int pages_count; //Número de pàgines que permet l'LCD
	  struct pcd8544_page *pages; //Array de pages.
	  unsigned long pseudo_palette[17]; //Estructura per implementar la funció de color, necessària per fer de consola.
	  int backlight; //Llum (no emprat)
};

static struct gpio lcd_gpio[] = {
/*
Estructura que fa referència als pins GPIO emprats.
*/
	{ 17, GPIOF_OUT_INIT_HIGH, "GPIO_RESET" },
	{ 25, GPIOF_OUT_INIT_HIGH, "GPIO_DC" },
};


static u8 pcd8544_spiblock[BLOCKLEN*4];
/*

*/





static void pcd8544_lcd_write(struct pcd8544 *item, unsigned char value, unsigned char isdata)
{
/*
Escriu el valor passat a l'LCD com a data o com a comanda.
*/
	u8 buf;
	buf = value;

	gpio_set_value(lcd_gpio[LCD_DC].gpio, isdata);
	//pcd8544_spi_write(item,value);
	spi_write(item->spidev, &buf, 1);
	
}

static void pcd8544_lcd_write_m(struct pcd8544 *item, unsigned char *buff, unsigned int len, unsigned char isdata)
{
/*
Escriu varis valors a l'LCD com a data o com a comanda.
*/

	gpio_set_value(lcd_gpio[LCD_DC].gpio, isdata);
	//pcd8544_spi_write(item,value);
	spi_write(item->spidev, buff, len);
	
}


static void Lcdpixelxy(struct pcd8544 *item, unsigned char x, unsigned char y)
{
	pcd8544_lcd_write(item, 0x40|(y&0x07),0);
	pcd8544_lcd_write(item, 0x80|(x&0x7f),0);
}




static int pcd8544_lcd_write_datablock(struct pcd8544 *item, unsigned short *block, int len)
{
	unsigned int x,y,i = 0;
	unsigned int p = 0;
	unsigned int xlines, ylines, pact;
/*
	La veritat és que el tio que va escriure aquest codi se la jugava molt declarant aquesta variable com a int.
	Vist que la x és un int, en sumar-la a un punter incrementava el punter en dos, per la qual cosa perquè tot
	funcionés bé len havia de tenir la meitat de memòria de la real.

	Com que amb els paràmetres inicials del programa (pensats per un monitor amb 2 bytes per píxel) necessitava 
	el doble de memòria ja li anava bé que fos un int, perquè així podia anar saltant de píxel en píxel. Quan és
	un sol byte, però, si salta de dos en dos corrs el risc de què no s'hagi inicialitzat la memòria suficient! 

	En definitiva, incrementam la x de dos en dos, però miram que no es passi del limit.
*/
	unsigned short value;
	unsigned char msb, lsb;
	unsigned char buff[item->info->var.xres*(item->info->var.yres/8)];
	//Omplirem un array de bytes amb tota la informació que ha d'anar a la pantalla.

	//ToDo: send in parts if needed
	if (len>BLOCKLEN) {
		dev_err(item->dev, "%s: len > blocklen (%i > %i)\n",
			   __func__, len, BLOCKLEN);
		len=BLOCKLEN;
	}

	dev_dbg(item->dev,"LENGHT: %i BLOCK: %p BUFF_SIZE: %i", len, block, item->info->var.xres*(item->info->var.yres/8) );
	xlines = item->info->var.xres;
	ylines = item->info->var.yres/8;
	for (x=0; 2*x<100; x++){
		//printk("Pos: %p valor: %04x \n", block+x, block[x]);
		dev_dbg(item->dev,"Pos: %p valor: %04x", block+x, block[x] );

	}
	//printk("\n\n------------------------------\n\n\n");

	memset((void *)buff, 0, item->info->var.xres*(item->info->var.yres/8));
	Lcdpixelxy(item,0,0);

	for (y=0; y<ylines; y++) {
		for (x=0; 2*x<xlines; x++) { //recorda que nou_ptr = ptr + 2*x
		/*
		És una guarrada, però aprofitam que aquest LCD ho guarda tot dins un bloc i actualitzam tota la pantalla de cop.
		Podríem tenir null pointers fent això si n'hi hagués més d'un, de bloc, però es que sino hem d'anar consultant vàries
		pàgines a la vegada en cas que estiguem al límit de la part inferior del byte que s'escriu.
		*/

		
			for (i=0; i<8; i++) {
				//fórmula del zig-zag
				//Comprovam que no ens passem dels límits:
				pact = (y*8+i)*42+x; //Com que en sumar el punter ens el multiplicarà per dos, nosaltres ja el divim entre dos.
				if (  2*pact < len && p<(item->info->var.xres*item->info->var.yres/8)){
					value = block[pact];
					msb = (value >> 8) & 0x0001;
					lsb = (unsigned char) value & 0x0001;			
					buff[p] = buff[p] | (lsb << i);
					buff[p+1] = buff[p+1] | (msb << i);
					
					dev_dbg(item->dev,"pact: %i p: %i x,y,i: %i %i %i bloc+pact:%p value = %04x msb = %02x lsb= %02x", pact, p, x,y,i, block+pact, value, msb, lsb );
				}
			}
			dev_dbg(item->dev,"buff(p) = %02x buff(p1) = %02x", buff[p], buff[p+1] );
			p=p+2;
		}

	}
	pcd8544_lcd_write_m(item, buff, p, 1);
	//recorda que nou_ptr = ptr + 2*x
	/*
		Ara toca passar la memòria interna del framebuffer a la memòria de l'LCD.
		Per això, cal tenir en compte que a l'LCD li enviam un byte que pinta els píxels en vertical.
		
		Ens caldrà trobar la manera, idò, d'agrupar els bytes de la memòria que han d'anar junts i enviar-los.
		Per això, es proposa recorrer la memòria fent zigzags. Si distribuim la memòria en una taula de 84x48,
		ens quedaria el següent:

		  c0     c1     c2     c3     c4     c5     c6     c7     ...      c83
		f0 ↓	/ ↓    / ↓	
		f1 ↓   /  ↓   /  ↓
		f2 ↓  /   ↓  /   ↓
		f3 ↓  /   ↓  /   ↓	...
		f4 ↓ /    ↓ /    ↓
		f5 ↓ /    ↓ /    ↓
		f6 ↓ /    ↓ /    ↓
		f7 ↓/     ↓/     ↓    
		----------------------------------------------------------------------------		
		f8 ↓	/ ↓	
		f9 ↓   /  ↓
		f10 ↓  /   ↓
		f11 ↓  /   ↓
		f12 ↓ /    ↓	...
		f13 ↓ /    ↓
		f14 ↓ /    ↓
		f15 ↓/     ↓		
		..
		f47

		Per la qual cosa la seqüència per accedir a la memòria serà 0, 84, 168,...,1, 85,...,2, 87...

	*/

	




		//msb = (value >> 8) & 0x00ff;
		//lsb = (unsigned char) value;
		//pcd8544_lcd_write(item, lsb, 1);
		//pcd8544_lcd_write(item, msb, 1);
		//dev_dbg(item->dev,"Bloc: %i valor: %02x", x, value );
		//dev_dbg(item->dev,"Bloc: %i len: %i", x, len );
		//pcd8544_spiblock[(x*4)]=0; //dummy
		//pcd8544_spiblock[(x*4)+1]=(CD4094_DC|CD4094_WR|(item->backlight?CD4094_BCNT:0))^CD4094_INVERT;
		//pcd8544_spiblock[(x*4)+2]=(value>>8)&0xff;
		//pcd8544_spiblock[(x*4)+3]=(value)&0xff;
	// spi_write(item->spidev, pcd8544_spiblock, len*4);
	return 0;
	}


static void pcd8544_dump_memory(struct pcd8544 *item)
{
/*
	Abocam tota la memòria al syslog
*/

	unsigned short *buffer;
	unsigned int i,j;
	for (i=0; i<item->pages_count; i++) {

		buffer = item->pages[i].buffer;
		dev_dbg(item->dev,
		"\n \n --- --- --- --- --- --- --- --- \n \n%s: page[%u]: x=%3hu y=%3hu buffer=0x%p len=%3hu\n\nDADES:\n\n",
		__func__, i, item->pages[i].x, item->pages[i].y, buffer ,  item->pages[i].len);

		for (j=0; 2*j<item->pages[i].len; j++) {
			//printk("%02x", buffer[j]);
		}

	}


}


static void pcd8544_copy(struct pcd8544 *item, unsigned int index)
{ //Posa el contingut de la posició index de la memòria al bus.
	unsigned short x;
	unsigned short y;
	unsigned short *buffer;
	unsigned int len;

	x = item->pages[index].x;
	y = item->pages[index].y;
	buffer = item->pages[index].buffer;
	len = item->pages[index].len;
	dev_dbg(item->dev,
	"%s: page[%u]: x=%3hu y=%3hu buffer=0x%p len=%3hu\n",
	__func__, index, x, y, buffer, len);

	
	/*Passam l'adreça on volem escriure*/
	Lcdpixelxy(item,x,y);
	//pcd8544_reg_set(item, pcd8544_REG_GDDRAM_X_ADDR, (item->info->var.yres - 1)-y);
	//pcd8544_reg_set(item, pcd8544_REG_GDDRAM_Y_ADDR, x);

	/* Deim a quina posició de lLCD volem escriure i
	 * escrivim */
		//pcd8544_lcd_write(item, pcd8544_REG_GDDRAM_DATA, 0);

		pcd8544_lcd_write_datablock(item, buffer, len);
	/*
	//The write_datablock can also be exchanged with this code, which is slower but
	//doesn't require the CD4020 counter IC.
	for (count = 0; count < len; count++) {
	pcd8544_spi_write(item, buffer[count], 1);
	}
	*/

	//pcd8544_lcd_write(item,0xff,1); //Funciona bé!

	//pcd8544_dump_memory(item);
}


static void pcd8544_update_all(struct pcd8544 *item)
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
 * &item->info->deferred_work: apunta a l'estructura pcd8544, i no sé que fa. Supos q actualitza. 
 * 
*/

}

static void pcd8544_update(struct fb_info *info, struct list_head *pagelist)
{
	struct pcd8544 *item = (struct pcd8544 *)info->par;
	struct page *page;
	int i;
	//printk("Soc a pcd8544_update :D");
	//We can be called because of pagefaults (mmap'ed framebuffer, pages
	//returned in *pagelist) or because of kernel activity
	//(pages[i]/must_update!=0). Add the former to the list of the latter.
	list_for_each_entry(page, pagelist, lru) {
		item->pages[page->index].must_update=1;
	}

	//Copy changed pages.
	for (i=0; i<item->pages_count; i++) {
	//ToDo: Small race here between checking and setting must_update,
	//maybe lock?
		if (item->pages[i].must_update) {
			item->pages[i].must_update=0;
	 		pcd8544_copy(item, i);
		}
	}
}


static void __init pcd8544_setup(struct pcd8544 *item)
{
/* Inicialització de l'LCD. 
 * El pcd8544_reg_set posa als pins gpio els valors requerits. (item, port sortida, valor)
 */
	dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);


	/* Inicialització de l'LCD. 
	 * El pcd8544_reg_set posa als pins gpio els valors requerits. (item, port sortida, valor)
	 */
	dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);
	
	gpio_set_value(lcd_gpio[LCD_RESET].gpio, 0); //Activam reset
	udelay(20);
	gpio_set_value(lcd_gpio[LCD_RESET].gpio, 1);
	udelay(120);
	dev_dbg(item->dev, "%s: item=0x%p\n", "SETUPDD", (void *)item);
	pcd8544_lcd_write(item,0x21,0);
	pcd8544_lcd_write(item,0xBf,0);
	pcd8544_lcd_write(item,0x04,0);
	pcd8544_lcd_write(item,0x14,0);
	pcd8544_lcd_write(item,0x0C,0);
	pcd8544_lcd_write(item,0x20,0);
	pcd8544_lcd_write(item,0x0C,0);
}






static int __init pcd8544_video_alloc(struct pcd8544 *item)
{
/*
This routine will allocate the buffer for the complete framebuffer. This
is one continuous chunk of 16-bit pixel values; userspace programs
will write here.
*/
	unsigned int frame_size;
	unsigned long length;

	dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);

	frame_size = item->info->fix.line_length * item->info->var.yres;
	//Calcul de la mida de la pantalla (frame)
	dev_dbg(item->dev, "%s: item=0x%p frame_size=%u\n",
		__func__, (void *)item, frame_size);

	item->pages_count = frame_size / PAGE_SIZE;
	//Calculam el número de pàgines agafant la mida de la pantalla entre la mida de les pàgines.
	//Com que la mida de les pàgines és gegant, segurament surti un valor ~0
 
	if ((item->pages_count * PAGE_SIZE) < frame_size) {
	//En cas que la mida necessària total no sigui exacta sumarem un al num de pàgines
		item->pages_count++;
	}
	dev_dbg(item->dev, "%s: item=0x%p pages_count=%u\n",
		__func__, (void *)item, item->pages_count);

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
	dev_dbg(item->dev, "smem_start: %p length: %i smem_end: %p", (unsigned long *) item->info->fix.smem_start, item->info->fix.smem_len  , ((unsigned long *) item->info->fix.smem_start)+length );


	return 0;
}

static void pcd8544_video_free(struct pcd8544 *item)
{
/*
Allibera la memòria del framebuffer
*/
	  dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);
	  vfree((void *)item->info->fix.smem_start);
}



static int __init pcd8544_pages_alloc(struct pcd8544 *item)
/*
This routine will allocate a pcd8544_page struct for each vm page in the
main framebuffer memory. Each struct will contain a pointer to the page
start, an x- and y-offset, and the length of the pagebuffer which is in the framebuffer.

A l'anterior rutina pcd8544_video_alloc() reservavem espai per l'usuari, pel framebuffer.
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
	unsigned int bytes_per_pixel;

	dev_dbg(item->dev, "%s: item=0x%p\n", __func__, (void *)item);


	item->pages = kmalloc(item->pages_count * sizeof(struct pcd8544_page),GFP_KERNEL);
	//Reservam l'espai que necessitarem a memòria virtual pels structs de les pàgines. 
	//Multiplicam, per això, el num de pagines per la mida de cada pàgina.
	if (!item->pages) {
		dev_err(item->dev, "%s: unable to kmalloc for pcd8544_page\n",
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

	dev_dbg(item->dev, "%s: item=0x%p pixels_per_page=%hu "
		"yoffset_per_page=%hu xoffset_per_page=%hu\n",
		__func__, (void *)item, pixels_per_page,
		yoffset_per_page, xoffset_per_page);

	buffer = (unsigned short *)item->info->fix.smem_start;
	/*
		El buffer apuntarà a les posicions de memòria reservades a la rutina anterior pcd8544_video_alloc
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
		dev_dbg(item->dev,"xres: %i yres: %i pixels_per_page: %i", item->info->var.xres , item->info->var.yres, pixels_per_page);

		len = (item->info->var.xres * item->info->var.yres) -   (index * pixels_per_page);
		if (len > pixels_per_page) {
			   len = pixels_per_page;
		}

		dev_dbg(item->dev,"%s: page[%d]: x=%3hu y=%3hu buffer=0x%p len=%3hu\n",__func__, index, x, y, buffer, len);

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


static void pcd8544_pages_free(struct pcd8544 *item)
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
static int pcd8544_setcolreg(unsigned regno, unsigned red, unsigned green, unsigned blue, unsigned transp, struct fb_info *info)
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
	
static int pcd8544_blank(int blank_mode, struct fb_info *info)
{
	  struct pcd8544 *item = (struct pcd8544 *)info->par;
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

static void pcd8544_touch(struct fb_info *info, int x, int y, int w, int h)
{
/*
	Actualitza una regió de la pantalla. 
*/
	struct fb_deferred_io *fbdefio = info->fbdefio;
	struct pcd8544 *item = (struct pcd8544 *)info->par;
	int i, ystart, yend;
	if (fbdefio) {
		//Touch the pages the y-range hits, so the deferred io will update them.
		for (i=0; i<item->pages_count; i++) {
			   ystart=item->pages[i].y;
			   yend=item->pages[i].y+(item->pages[i].len/info->fix.line_length)+1;
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
static void pcd8544_fillrect(struct fb_info *p, const struct fb_fillrect *rect)
{
	sys_fillrect(p, rect);
	pcd8544_touch(p, rect->dx, rect->dy, rect->width, rect->height);
}

static void pcd8544_imageblit(struct fb_info *p, const struct fb_image *image)
{
	sys_imageblit(p, image);
	pcd8544_touch(p, image->dx, image->dy, image->width, image->height);
}

static void pcd8544_copyarea(struct fb_info *p, const struct fb_copyarea *area)
{
	sys_copyarea(p, area);
	pcd8544_touch(p, area->dx, area->dy, area->width, area->height);
}

static ssize_t pcd8544_write(struct fb_info *p, const char __user *buf,
						 size_t count, loff_t *ppos)
{
	ssize_t res;
	int i;

	//for (i=0; i<count; i++){
	//	printk( "\n\n----------------\n\nPCDWRITE - M'has passat un %02x \n\n\n----------------\n\n", buf[i]);
	//}


	res = fb_sys_write(p, buf, count, ppos);
	/*
		És un simple copy_from_user.
	*/

	pcd8544_touch(p, 0, 0, p->var.xres, p->var.yres);
	return res;
}

/*
	A continuació venen les estructures de dades del framebuffer.
*/

static struct fb_ops pcd8544_fbops = {
/*
	Punters a les funcions a implementar.
*/
	  .owner	   = THIS_MODULE,
	  .fb_read	 = fb_sys_read,
	  .fb_write	= pcd8544_write,
	  .fb_fillrect  = pcd8544_fillrect,
	  .fb_copyarea  = pcd8544_copyarea,
	  .fb_imageblit = pcd8544_imageblit,
	  .fb_setcolreg   = pcd8544_setcolreg,
	  .fb_blank	  = pcd8544_blank,
};
	
static struct fb_fix_screeninfo pcd8544_fix __initdata = {
/*
	Dades inicials del pcd8544
*/
	.id		= "pcd8544",
	.type	   = FB_TYPE_PACKED_PIXELS,
	.visual	 = FB_VISUAL_TRUECOLOR,
	.accel	  = FB_ACCEL_NONE,
	.line_length = 84 * BYTES_PER_PIXEL,

};

static struct fb_var_screeninfo pcd8544_var __initdata = {
	.xres		 = 84,
	.yres		 = 48,
	.xres_virtual   = 84,
	.yres_virtual   = 48,
	.width		= 84,
	.height	    = 48,
	.bits_per_pixel = 8*BYTES_PER_PIXEL,
	.red		  = {11, 5, 0},
	.green		= {5, 6, 0},
	.blue		 = {0, 5, 0},
	.activate	  = FB_ACTIVATE_NOW,
	.vmode		= FB_VMODE_NONINTERLACED,
};

static struct fb_deferred_io pcd8544_defio = {
/*
	Funcions IO del framebuffer
*/
	  .delay		= HZ / 20,
	  .deferred_io    = &pcd8544_update,
/*

	Deferred_io és la funció a la que cridarà el kernel amb el framebuffer en haver d'enviar les dades al dispositiu extern(LCD)
	Per tant qualsevol operació d'escriptura cridarà a pcd8544_update 

*/
};

static int __devinit pcd8544_probe(struct spi_device *dev)
{
/*

	Funció que s'executa quan es detecta un dispositiu. Assignam 
	memòria a les estructures de dades del framebuffer i del dispositiu.
	Haurem de copiar les dades que ens venguin del *dev a la nostra
	estructura de dades

*/
	int ret = 0;
	struct pcd8544 *item;
	struct fb_info *info;

	dev_dbg(&dev->dev, "%s\n", __func__);

	item = kzalloc(sizeof(struct pcd8544), GFP_KERNEL);
	//Assignam memòria a l'estructura del LCD

	if (!item) {
		dev_err(&dev->dev,
			"%s: unable to kzalloc for pcd8544\n", __func__);
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

	info = framebuffer_alloc(sizeof(struct pcd8544), &dev->dev);
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
	info->fbops = &pcd8544_fbops;
	info->flags = FBINFO_FLAG_DEFAULT|FBINFO_VIRTFB;
	info->fix = pcd8544_fix;
	info->var = pcd8544_var;
	//Copiam informació vària

	ret = pcd8544_video_alloc(item);
	//reservam la memòria de video
	if (ret) {
		dev_err(&dev->dev,
			   "%s: unable to pcd8544_video_alloc\n", __func__);
		goto out_info;
	}
	info->screen_base = (char __iomem *)item->info->fix.smem_start;

	ret = pcd8544_pages_alloc(item);
	//reservam la memòria per les pàgines
	if (ret < 0) {
		dev_err(&dev->dev,
			   "%s: unable to pcd8544_pages_init\n", __func__);
		goto out_video;
	}

	info->fbdefio = &pcd8544_defio;
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
	pcd8544_setup(item);
	//Funció d'inicialització de l'LCD
	pcd8544_update_all(item);
	//Fes net l'LCD 

	return ret;

out_pages:
	  pcd8544_pages_free(item);
out_video:
	  pcd8544_video_free(item);
out_info:
	  framebuffer_release(info);
out_item:
	  kfree(item);
out:
	  return ret;
}
	
static int __devexit pcd8544_remove(struct spi_device *dev)
/*
	Funció que neteja el device.
*/
{
	  struct pcd8544 *item = dev_get_drvdata(&dev->dev);
	  struct fb_info *info;

	  dev_dbg(&dev->dev, "%s\n", __func__);

	  dev_set_drvdata(&dev->dev, NULL);
	  if (item) {
			info = item->info;
			if (info)
				   unregister_framebuffer(info);
			pcd8544_pages_free(item);
			pcd8544_video_free(item);
			kfree(item);
			if (info)
				   framebuffer_release(info);
	  }
	  return 0;
}


/*	
	No ens interessen aquestes funcions, no existeixen al lcd
*/
#define pcd8544_suspend NULL
#define pcd8544_resume NULL

static struct spi_driver spi_pcd8544_driver = {
/*
	Assignam les funcions del driver spi
*/
	  .driver = {
			.name   = "spi-ili9325",
			.bus    = &spi_bus_type,
			.owner  = THIS_MODULE,
	  },
	  .probe = pcd8544_probe,
	  .remove = pcd8544_remove,
	  .suspend = pcd8544_suspend,
	  .resume = pcd8544_resume,
};



static int __init pcd8544_init(void)
/*
	Funció que es crida quan es carrega el mòdul.
	Registra els drivers i carrega els gpios necessaris.
*/
{
	int ret = 0;

	pr_debug("PAGE_SIZE:%lu%s\n",PAGE_SIZE ,__func__);

	ret = spi_register_driver(&spi_pcd8544_driver);
	if (ret) {
		pr_err("%s: unable to platform_driver_register\n", __func__);
	}
	ret = gpio_request_array(lcd_gpio, ARRAY_SIZE(lcd_gpio));
	gpio_set_value(lcd_gpio[LCD_RESET].gpio, 1);
	if (ret) {
		pr_err("%s: unable to get GPIOs\n", __func__);
	}
	return ret;
}
	
static void __exit pcd8544_exit(void)
/*
	Funció que es crida quan es treu el mòdul.
	Allibera els drivers i l'espai.
*/
{
	pr_debug("%s\n", __func__);

	spi_unregister_driver(&spi_pcd8544_driver);
	gpio_set_value(lcd_gpio[LCD_RESET].gpio, 0);		  
	gpio_free_array(lcd_gpio, ARRAY_SIZE(lcd_gpio));

}

module_init(pcd8544_init);
module_exit(pcd8544_exit);

MODULE_DESCRIPTION("pcd8544 LCD Driver");
MODULE_AUTHOR("Jeroen Domburg <jeroen@spritesmods.com>");
MODULE_LICENSE("GPL");
