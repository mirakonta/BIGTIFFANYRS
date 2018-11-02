#include "wrnum.h"

#include "libmflcd.h"
#define uart_tx(x)           do { lcd_writedata(x); lcd_waitshort(); } while (0)
#define uart_writenum32      lcd_writenum32


uint8_t uart_writenum32(uint32_t val, uint8_t nrdig1, uint8_t flags1)
{
	char __autodata ch = 0;
	uint8_t __autodata d;
	uint8_t __autodata cnt = 10;
	uint8_t __autodata flags = flags1;
	uint8_t __autodata nrdig = nrdig1;

	if ((flags & WRNUM_SIGNED) && ((int32_t)val) < 0) {
		val = -val;
		ch = '-';
	} else if ((flags & WRNUM_ZEROPLUS) || ((flags & WRNUM_PLUS) && val)) {
		ch = '+';
	}
	if (ch && nrdig > 0)
		--nrdig;
	if (flags & WRNUM_TSDSEP) {
		if (nrdig > 9)
			--nrdig;
		if (nrdig > 6)
			--nrdig;
		if (nrdig > 3)
			--nrdig;
	}
	flags &= WRNUM_MASK;
	if (cnt < nrdig)
		cnt = nrdig;
	do {
		d = cnt;
		val = libmf_num32_digit(val, (uint8_t __data *)&d);
		if (!d && cnt != 1 && !(flags & WRNUM_DIGCONT)) {
			if (cnt > nrdig)
				continue;
			if (!(flags & WRNUM_PADZERO)) {
				if (!(flags & WRNUM_DIGSET)) {
					nrdig = cnt;
					flags |= WRNUM_DIGSET;
				}
				uart_tx(' ');
				if ((flags & WRNUM_TSDSEP) && (cnt == 4 || cnt == 7 || cnt == 10)) {
					uart_tx(' ');
					++nrdig;
				}
				continue;
			}
		}
		if (!(flags & WRNUM_DIGCONT)) {
			if (!(flags & WRNUM_DIGSET))
				nrdig = cnt;
			flags |= WRNUM_PADZERO | WRNUM_DIGCONT | WRNUM_DIGSET;
			if (ch) {
				uart_tx(ch);
				++nrdig;
			}
		}
		uart_tx('0' + d);
		if ((flags & WRNUM_TSDSEP) && (cnt == 4 || cnt == 7 || cnt == 10)) {
			uart_tx('\'');
			++nrdig;
		}
	} while (--cnt);
	return nrdig;
}

