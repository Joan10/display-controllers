#AMB AQUEST NO EM DEIXA AL SPI0.0 SHA DE POSAR EL SPI0.1, NO SURT, PER TANT CAL CANVIAR EL CE DE POSICIO. AMB EL KERNEL ESTA BE AL SPI0.0

import RPi.GPIO as GPIO  
import spidev            
import time                 
import struct

cs = 18                  
cd = 22                  
wr = 27                  
rd = 17                  
rst = 4                  
                         
IDLE = True              
ACTIVE = False  

TFTWIDTH   = 240
TFTHEIGHT  = 320

ILI932X_START_OSC          ="0x00"
ILI932X_DRIV_OUT_CTRL      ="0x01"
ILI932X_DRIV_WAV_CTRL      ="0x02"
ILI932X_ENTRY_MOD          ="0x03"
ILI932X_RESIZE_CTRL        ="0x04"
ILI932X_DISP_CTRL1         ="0x07"
ILI932X_DISP_CTRL2         ="0x08"
ILI932X_DISP_CTRL3         ="0x09"
ILI932X_DISP_CTRL4         ="0x0A"
ILI932X_RGB_DISP_IF_CTRL1  ="0x0C"
ILI932X_FRM_MARKER_POS     ="0x0D"
ILI932X_RGB_DISP_IF_CTRL2  ="0x0F"
ILI932X_POW_CTRL1          ="0x10"
ILI932X_POW_CTRL2          ="0x11"
ILI932X_POW_CTRL3          ="0x12"
ILI932X_POW_CTRL4          ="0x13"
ILI932X_GRAM_HOR_AD        ="0x20"
ILI932X_GRAM_VER_AD        ="0x21"
ILI932X_RW_GRAM            ="0x22"
ILI932X_POW_CTRL7          ="0x29"
ILI932X_FRM_RATE_COL_CTRL  ="0x2B"
ILI932X_GAMMA_CTRL1        ="0x30"
ILI932X_GAMMA_CTRL2        ="0x31"
ILI932X_GAMMA_CTRL3        ="0x32"
ILI932X_GAMMA_CTRL4        ="0x35"
ILI932X_GAMMA_CTRL5        ="0x36"
ILI932X_GAMMA_CTRL6        ="0x37"
ILI932X_GAMMA_CTRL7        ="0x38"
ILI932X_GAMMA_CTRL8        ="0x39"
ILI932X_GAMMA_CTRL9        ="0x3C"
ILI932X_GAMMA_CTRL10       ="0x3D"
ILI932X_HOR_START_AD       ="0x50"
ILI932X_HOR_END_AD         ="0x51"
ILI932X_VER_START_AD       ="0x52"
ILI932X_VER_END_AD         ="0x53"
ILI932X_GATE_SCAN_CTRL1    ="0x60"
ILI932X_GATE_SCAN_CTRL2    ="0x61"
ILI932X_GATE_SCAN_CTRL3    ="0x6A"
ILI932X_PART_IMG1_DISP_POS ="0x80"
ILI932X_PART_IMG1_START_AD ="0x81"
ILI932X_PART_IMG1_END_AD   ="0x82"
ILI932X_PART_IMG2_DISP_POS ="0x83"
ILI932X_PART_IMG2_START_AD ="0x84"
ILI932X_PART_IMG2_END_AD   ="0x85"
ILI932X_PANEL_IF_CTRL1     ="0x90"
ILI932X_PANEL_IF_CTRL2     ="0x92"
ILI932X_PANEL_IF_CTRL3     ="0x93"
ILI932X_PANEL_IF_CTRL4     ="0x95"
ILI932X_PANEL_IF_CTRL5     ="0x97"
ILI932X_PANEL_IF_CTRL6     ="0x98"
TFTLCD_DELAY = "0xFF"

llista_inici = [ILI932X_START_OSC        , "0x0001",  TFTLCD_DELAY             , "50",       ILI932X_DRIV_OUT_CTRL    , "0x0100",  ILI932X_DRIV_WAV_CTRL    , "0x0700",  ILI932X_ENTRY_MOD        , "0x1030",  ILI932X_RESIZE_CTRL      , "0x0000",  ILI932X_DISP_CTRL2       , "0x0202",  ILI932X_DISP_CTRL3       , "0x0000",  ILI932X_DISP_CTRL4       , "0x0000",  ILI932X_RGB_DISP_IF_CTRL1, "0x0",  ILI932X_FRM_MARKER_POS   , "0x0",  ILI932X_RGB_DISP_IF_CTRL2, "0x0",  ILI932X_POW_CTRL1        , "0x0000",  ILI932X_POW_CTRL2        , "0x0007",  ILI932X_POW_CTRL3        , "0x0000",  ILI932X_POW_CTRL4        , "0x0000",  TFTLCD_DELAY             , "200",  ILI932X_POW_CTRL1        , "0x1690",  ILI932X_POW_CTRL2        , "0x0227",  TFTLCD_DELAY             , "50",  ILI932X_POW_CTRL3        , "0x001A",  TFTLCD_DELAY             , "50",  ILI932X_POW_CTRL4        , "0x1800",  ILI932X_POW_CTRL7        , "0x002A",  TFTLCD_DELAY             , "50",  ILI932X_GAMMA_CTRL1      , "0x0000",  ILI932X_GAMMA_CTRL2      , "0x0000",  ILI932X_GAMMA_CTRL3      , "0x0000",  ILI932X_GAMMA_CTRL4      , "0x0206",  ILI932X_GAMMA_CTRL5      , "0x0808",  ILI932X_GAMMA_CTRL6      , "0x0007",  ILI932X_GAMMA_CTRL7      , "0x0201",  ILI932X_GAMMA_CTRL8      , "0x0000",  ILI932X_GAMMA_CTRL9      , "0x0000",  ILI932X_GAMMA_CTRL10     , "0x0000",  ILI932X_GRAM_HOR_AD      , "0x0000",  ILI932X_GRAM_VER_AD      , "0x0000",  ILI932X_HOR_START_AD     , "0x0000",  ILI932X_HOR_END_AD       , "0x00EF",  ILI932X_VER_START_AD     , "0X0000",  ILI932X_VER_END_AD       , "0x013F",  ILI932X_GATE_SCAN_CTRL1  , "0xA700",   ILI932X_GATE_SCAN_CTRL2  , "0x0003",   ILI932X_GATE_SCAN_CTRL3  , "0x0000",   ILI932X_PANEL_IF_CTRL1   , "0X0010",   ILI932X_PANEL_IF_CTRL2   , "0X0000",  ILI932X_PANEL_IF_CTRL3   , "0X0003",  ILI932X_PANEL_IF_CTRL4   , "0X1100",  ILI932X_PANEL_IF_CTRL5   , "0X0000",  ILI932X_PANEL_IF_CTRL6   , "0X0000",  ILI932X_DISP_CTRL1       , "0x0133"   ]

def wr_strobe():
	GPIO.output(wr, ACTIVE)
	GPIO.output(wr, IDLE)
	

def write8_spi0(d):  
	#print("write_spi0")
	#print(d)
	#print("\n")
	spi.xfer2(d)
	wr_strobe()
	

def write8_spi(d):  
	d0 = int(d,16)
	write8_spi0([d0])
	
def writeRegister16(c, d):
	#Primer param: Comanda, (en string)
	#Segon param: Dades ( en string)
	#print("\nWRITE REG 16\n")
	c0 = int(c,16)
	d0 = int(d,16)
	
	lo = c0 & 0x00ff
	hi = (c0 >> 8) & 0x00ff 
	#print("\n Escrivim c:")
	#print(c)
	#print ("\n Escrivim d: ")
	#print(d)
	


	GPIO.output(cd, ACTIVE) #Command
	
	write8_spi0([hi])
	#print("\nHi c:")
	#print(hi)
	#raw_input("Seguir")
	write8_spi0([lo])
	#print("\nLo c:")
	#print(lo)
	#raw_input("Seguir")
	
	lo = d0 & 0x00ff
	hi = (d0 >> 8) & 0x00ff 
	
	GPIO.output(cd, IDLE) #data
	
	write8_spi0([hi])
	##print("\nHi d:")
	#print(hi)
	#raw_input("Seguir")
	write8_spi0([lo])
	#print("\nLo d:")
	#print(lo)
	#raw_input("Seguir")
	
      #  print("\n\n-------------------------\n\n")                 
#Els senyals s'activen a nivell baix.
def reset():               
	GPIO.output(cs, IDLE)                                                                                                        
	GPIO.output(cd, IDLE)
	GPIO.output(wr, IDLE)
	GPIO.output(rd, IDLE)
	GPIO.output(rst, ACTIVE)
	time.sleep(0.01)     
	GPIO.output(rst, IDLE)
	GPIO.output(cs, ACTIVE)                                                                                                            
	GPIO.output(cd, ACTIVE)
	write8_spi0([0x00])                                                                                                               
	for i in range(8):
		wr_strobe()
	GPIO.output(cs, IDLE)
	time.sleep(0.1) 
	
def init_lcd():
	#a="01"
	#b=struct.pack("h",int(a,16))
	#b[x]
	GPIO.output(cs, ACTIVE)
	#print("Sequencia init\n")
	i=0
	while i<len(llista_inici):
		a = llista_inici[i]
		i=i+1
		b = llista_inici[i]
		i=i+1
		if a == TFTLCD_DELAY :
			time.sleep(int(b,10)*0.001);
		else:
			writeRegister16(a,b)
			
	setAddrWindow("0", "0", "0x00F0", "0x0140")
	GPIO.output(cs, IDLE)
			

	
def setAddrWindow( x1, y1, x2, y2):
#params: str
	
	GPIO.output(cs, ACTIVE)
	x  = x1;
	y  = y1;
	writeRegister16("0x0050", x1);
	writeRegister16("0x0051", x2);
	writeRegister16("0x0052", y1);
	writeRegister16("0x0053", y2);
	writeRegister16("0x0020", x );
	writeRegister16("0x0021", y );
	GPIO.output(cs, IDLE)

def pinta():
	GPIO.output(cs, ACTIVE)
	GPIO.output(cd, ACTIVE)
	write8_spi("0x00")
	write8_spi("0x22")
	GPIO.output(cd, IDLE)
	write8_spi("0xff")
	write8_spi("0xff")
	
	for i in range(TFTWIDTH*TFTHEIGHT):
		wr_strobe()
		wr_strobe()

	GPIO.output(cs, IDLE)

def pinta_pix(pos_x, pos_y, color):
	GPIO.output(cs, ACTIVE)


	writeRegister16("0x0020", pos_x );
	writeRegister16("0x0021", pos_y );

	writeRegister16("0x0022", color );
	GPIO.output(cs, IDLE)

spi = spidev.SpiDev()
spi.open(0,1)            

GPIO.setwarnings(False)                     
GPIO.setmode(GPIO.BCM)
GPIO.setup(18, GPIO.OUT)
GPIO.setup(22, GPIO.OUT)
GPIO.setup(27, GPIO.OUT)
GPIO.setup(17, GPIO.OUT)
GPIO.setup(4, GPIO.OUT)
GPIO.output(rst, IDLE)

reset();
init_lcd()

GPIO.output(cs, ACTIVE)
writeRegister16("0x0020", "0")
writeRegister16("0x0021", "0")
GPIO.output(cs, IDLE)
#pinta()

setAddrWindow( "0x0000", "0x0000", "0x00ef", "0x013f")
pinta()
setAddrWindow("0", "0", "0x00F0", "0x0140")
pinta_pix("0x0021","0x0054", "0x0031" )
pinta_pix("0x0022","0x0054", "0x0031" )
pinta_pix("0x0023","0x0054", "0x0031" )
pinta_pix("0x0024","0x0054", "0x0031" )
pinta_pix("0x0025","0x0054", "0x0031" )
pinta_pix("0x0026","0x0054", "0x0031" )
pinta_pix("0x0021","0x0055", "0x0031" )
pinta_pix("0x0022","0x0055", "0x0031" )
pinta_pix("0x0023","0x0055", "0x0031" )
pinta_pix("0x0024","0x0055", "0x0031" )
pinta_pix("0x0025","0x0055", "0x0031" )

while True:
	setAddrWindow("0", "0", "0x00F0", "0x0140")
	pinta_pix("0x0021","0x0054", "0x0031" )
	pinta_pix("0x0022","0x0054", "0x0031" )
	pinta_pix("0x0023","0x0054", "0x0031" )
	pinta_pix("0x0024","0x0054", "0x0031" )
	pinta_pix("0x0025","0x0054", "0x0031" )
	pinta_pix("0x0026","0x0054", "0x0031" )
	pinta_pix("0x0021","0x0055", "0x0031" )
	pinta_pix("0x0022","0x0055", "0x0031" )
	pinta_pix("0x0023","0x0055", "0x0031" )
	pinta_pix("0x0024","0x0055", "0x0031" )
	pinta_pix("0x0025","0x0055", "0x0031" )
