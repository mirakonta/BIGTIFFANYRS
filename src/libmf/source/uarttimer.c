#include "ax8052.h"
#include "libmfuart.h"

#if TIMER == 0
#define uart_timer_baud uart_timer0_baud
#define TCLKSRC T0CLKSRC
#define TMODE   T0MODE
#define TPERIOD T0PERIOD
#elif TIMER == 1
#define uart_timer_baud uart_timer1_baud
#define TCLKSRC T1CLKSRC
#define TMODE   T1MODE
#define TPERIOD T1PERIOD
#elif TIMER == 2
#define uart_timer_baud uart_timer2_baud
#define TCLKSRC T2CLKSRC
#define TMODE   T2MODE
#define TPERIOD T2PERIOD
#else
#error "TIMER not set"
#endif

__reentrantb void uart_timer_baud(uint8_t clksrc, uint32_t baud, uint32_t clkfreq) __reentrant
{
	uint8_t sh = 26;
	while (sh) {
		uint8_t bdhi = baud >> 24;
		if (!bdhi && sh >= 8) {
			baud <<= 8;
			sh -= 8;
			continue;
		}
		if (!(bdhi & 0xF0) && sh >= 4) {
			baud <<= 4;
			sh -= 4;
			continue;
		}
		if (!(bdhi & 0xC0) && sh >= 2) {
			baud <<= 2;
			sh -= 2;
			continue;
		}
		if (!(bdhi & 0x80)) {
			baud <<= 1;
			--sh;
			continue;
		}
		break;
	}
	clkfreq >>= sh;
	baud /= clkfreq;
	sh = 0x38;
	while (baud >= 16384 && (sh & 0xF0)) {
		baud >>= 1;
		sh -= 8;
	}
	TCLKSRC = sh | (clksrc & 7);
	TMODE = 0x04;
	TPERIOD = baud;
}
