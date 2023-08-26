/*
 *	Additional logic on the P90CE201
 */

#include <stdint.h>
#include <stdio.h>
#include "system.h"
#include "p90ce201.h"

static uint16_t syscon1;
static uint16_t syscon2;
static uint8_t scon;
static uint8_t gpp;
static uint8_t gp;
static uint8_t aux;
static uint8_t auxcon;
static uint16_t rcap0;
static uint8_t t1con;
static uint8_t t2con;

uint8_t p90_read(uint32_t addr)
{
    unsigned r;
    switch(addr & 0xFFFF) {
    case 0x1000:		/* control 1 */
        return syscon1 >> 8;
    case 0x1001:
        return syscon1;
    case 0x1002:		/* control 2* */
        return syscon2 >> 8;
    case 0x1003:
        return syscon2;
    case 0x2021:		/* Sbuf */
        return next_char();
    case 0x2023:		/* Scon */
        scon &= 0xFC;
        r = check_chario();
        /* Emulate the pending input and output bits */
        scon |= r;
/*        if (r & 2)
            scon |= 1;
        if (r & 1)
            scon |= 2; */
        return scon;
    case 0x2025:		/* Uart rx int */
    case 0x2029:		/* UART tx int */
    case 0x2032:		/* Timer reload 0 high */
    case 0x2033:		/* TImer 0 reload low */
    case 0x2035:		/* Timer 0 control */
    case 0x2037:		/* Timer 0 interrupt */
    case 0x2045:		/* Timer 1 control */
    case 0x2050:		/* Timer 2 reload high */
    case 0x2051:		/* Timer 2 reload low */
    case 0x2055:		/* Timer 2 control */
        break;
    case 0x2071:		/* GP pad */
        return gpp;
        break;
    case 0x2073:		/* GP register */
        return gp;
    case 0x2081:		/* Aux */
        /* TODO: call back to read aux lines */
        return aux;
    case 0x2803:		/* Aux control */
        return auxcon;
    default:
        fprintf(stderr, "Unemulated P90 read %06X", addr);
    }
    return 0xFF; 
}

void p90_write(uint32_t addr, uint8_t val)
{
    switch(addr & 0xFFFF) {
    case 0x1000:		/* control 1 */
        syscon1 &= 0xFF;	/* Wait states etc */
        syscon1 |=  val << 8;
        break;
    case 0x1001:
        syscon1 &= 0xFF00;
        syscon1 |=  val ;
        break;
    case 0x1002:		/* control 2* */
        syscon2 &= 0xFF;
        syscon2 |=  val << 8;
        break;
    case 0x1003:
        syscon2 &= 0xFF00;
        syscon2 |=  val ;
        break;
    case 0x2021:		/* Sbuf */
        putchar(val);
        fflush(stdout);
        break;			/* Minimal emulation will do fine for now */
    case 0x2023:		/* Scon */
        scon = val;
        break;
    case 0x2025:		/* Uart rx int */
    case 0x2029:		/* UART tx int */
        break;
    case 0x2032:		/* Timer reload 0 high */
        rcap0 &= 0xFF;
        rcap0 |= val << 8;
        break;
    case 0x2033:		/* TImer 0 reload low */
        rcap0 &= 0xFF00;
        rcap0 |= val;
        break;
    case 0x2035:		/* Timer 0 control */
    case 0x2037:		/* Timer 0 interrupt */
        break;
    case 0x2045:		/* Timer 1 control */
        t1con = val;
        break;
    case 0x2050:		/* Timer 2 reload high */
    case 0x2051:		/* Timer 2 reload low */
        break;
    case 0x2055:		/* Timer 2 control */
        t2con = val;
        break;
    case 0x2081:		/* Aux */
        aux = val;
        /* Notify the system the lines have changed */
        p90_set_aux(auxcon, aux);
        break;
    case 0x2083:		/* Aux control */
        auxcon = val;
        /* Notify the system the lines have changed */
        p90_set_aux(auxcon, aux);
        break;
    default:
        fprintf(stderr, "Unemulated P90 write %06X,%02X", addr, val);
    }    
}
