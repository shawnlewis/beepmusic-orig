#ifndef _FLASH_H
#define _FLASH_H


////// Shawn

//#include "ar7240_soc.h"
#include "types.h"

// This is for 4MB flash
#define CFG_MAX_FLASH_BANKS     1	    /* max number of memory banks */
#define CFG_MAX_FLASH_SECT      64    /* max number of sectors on one chip */
#define CFG_FLASH_SECTOR_SIZE   (64*1024)
#define CFG_FLASH_SIZE          0x00400000 /* Total flash size */

// different from when in u-boot, which uses memory map.
#define CFG_FLASH_BASE		    0x0

typedef struct {
	ulong	size;			/* total bank size in bytes		*/
	ushort	sector_count;		/* number of erase units		*/
	ulong	flash_id;		/* combined device & manufacturer code	*/
	ulong	start[CFG_MAX_FLASH_SECT];   /* physical sector start addresses */
	uchar	protect[CFG_MAX_FLASH_SECT]; /* sector protection status	*/
#ifdef CFG_FLASH_CFI
	uchar	portwidth;		/* the width of the port		*/
	uchar	chipwidth;		/* the width of the chip		*/
	ushort	buffer_size;		/* # of bytes in write buffer		*/
	ulong	erase_blk_tout;		/* maximum block erase timeout		*/
	ulong	write_tout;		/* maximum write timeout		*/
	ulong	buffer_write_tout;	/* maximum buffer write timeout		*/
	ushort	vendor;			/* the primary vendor id		*/
	ushort	cmd_reset;		/* Vendor specific reset command	*/
	ushort	interface;		/* used for x8/x16 adjustments		*/
	ushort	legacy_unlock;		/* support Intel legacy (un)locking	*/
#endif
} flash_info_t;

#define KSEG1			0xa0000000
#define KSEG1ADDR(a)		((__typeof__(a))(((unsigned long)(a) & 0x1fffffff) | KSEG1))

#define ar7240_reg_rd(_phys)    (*(volatile unsigned int *)KSEG1ADDR(_phys))
#define ar7240_reg_wr_nf(_phys, _val) \
                    ((*(volatile unsigned int *)KSEG1ADDR(_phys)) = (_val))

#define ar7240_reg_wr(_phys, _val) do {     \
                    ar7240_reg_wr_nf(_phys, _val);  \
                    ar7240_reg_rd(_phys);       \
}while(0);

#define ar7240_reg_rmw_set(_reg, _mask)  do {                        \
    ar7240_reg_wr((_reg), (ar7240_reg_rd((_reg)) | (_mask)));      \
    ar7240_reg_rd((_reg));                                           \
}while(0);

#define ar7240_reg_rmw_clear(_reg, _mask)  do {                        \
    ar7240_reg_wr((_reg), (ar7240_reg_rd((_reg)) & ~(_mask)));      \
    ar7240_reg_rd((_reg));                                           \
}while(0);

////// End Shawn

#define AR7240_SPI_FS           0x1f000000
#define AR7240_SPI_CLOCK        0x1f000004
#define AR7240_SPI_WRITE        0x1f000008
#define AR7240_SPI_READ         0x1f000000
#define AR7240_SPI_RD_STATUS    0x1f00000c

#define AR7240_SPI_CS_DIS       0x70000
#define AR7240_SPI_CE_LOW       0x60000
#define AR7240_SPI_CE_HIGH      0x60100

#define AR7240_SPI_CMD_WRITE_SR     0x01
#define AR7240_SPI_CMD_WREN         0x06
#define AR7240_SPI_CMD_RD_STATUS    0x05
#define AR7240_SPI_CMD_FAST_READ    0x0b
#define AR7240_SPI_CMD_PAGE_PROG    0x02
#define AR7240_SPI_CMD_SECTOR_ERASE 0xd8
#define AR7240_SPI_CMD_CHIP_ERASE   0xc7
#define AR7240_SPI_CMD_RDID         0x9f

#define AR7240_SPI_SECTOR_SIZE      (1024*64)
#define AR7240_SPI_PAGE_SIZE        256


#define display(_x)     ar7240_reg_wr_nf(0x18040008, (_x))

/*
 * primitives
 */

#define ar7240_be_msb(_val, _i) (((_val) & (1 << (7 - _i))) >> (7 - _i))

#define ar7240_spi_bit_banger(_byte)  do {        \
    int i;                                      \
    for(i = 0; i < 8; i++) {                    \
        ar7240_reg_wr_nf(AR7240_SPI_WRITE,      \
                        AR7240_SPI_CE_LOW | ar7240_be_msb(_byte, i));  \
        ar7240_reg_wr_nf(AR7240_SPI_WRITE,      \
                        AR7240_SPI_CE_HIGH | ar7240_be_msb(_byte, i)); \
    }       \
}while(0);

#define ar7240_spi_go() do {        \
    ar7240_reg_wr_nf(AR7240_SPI_WRITE, AR7240_SPI_CE_LOW); \
    ar7240_reg_wr_nf(AR7240_SPI_WRITE, AR7240_SPI_CS_DIS); \
}while(0);


#define ar7240_spi_send_addr(__a) do {			\
    ar7240_spi_bit_banger(((__a & 0xff0000) >> 16));	\
    ar7240_spi_bit_banger(((__a & 0x00ff00) >> 8));	\
    ar7240_spi_bit_banger(__a & 0x0000ff);		\
} while (0)

#define ar7240_spi_delay_8()    ar7240_spi_bit_banger(0)
#define ar7240_spi_done()       ar7240_reg_wr_nf(AR7240_SPI_FS, 0)

extern unsigned long flash_get_geom (flash_info_t *flash_info);

#endif /*_FLASH_H*/
