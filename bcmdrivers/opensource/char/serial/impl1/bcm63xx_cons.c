/*
<:copyright-gpl 
 Copyright 2002 Broadcom Corp. All Rights Reserved. 
 
 This program is free software; you can distribute it and/or modify it 
 under the terms of the GNU General Public License (Version 2) as 
 published by the Free Software Foundation. 
 
 This program is distributed in the hope it will be useful, but WITHOUT 
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or 
 FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License 
 for more details. 
 
 You should have received a copy of the GNU General Public License along 
 with this program; if not, write to the Free Software Foundation, Inc., 
 59 Temple Place - Suite 330, Boston MA 02111-1307, USA. 
:>
*/

/* Description: Serial port driver for the BCM963XX. */

#include <linux/autoconf.h>
#include <linux/module.h>
#include <linux/tty.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/serial.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/device.h>
#include <linux/version.h>

#include <board.h>
#include <bcm_map_part.h>
#include <bcm_intr.h>
#include <linux/bcm_colors.h>

#if defined(CONFIG_BCM_SERIAL_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
static char sysrq_start_char='^';
#define SUPPORT_SYSRQ
#endif

#if 1 /* ChenHe@MSTC for disable console output when debug flag is 0, 20130524 */
static char uartDebugFlag = 0;
#endif

#include <linux/serial_core.h>

#if defined UART1_BASE
#define UART_NR     2
#else
#define UART_NR     1
#endif

#define PORT_BCM63XX    63

#define BCM63XX_ISR_PASS_LIMIT  256

#define UART_REG(p) ((volatile Uart *) (p)->membase)

static void bcm63xx_stop_tx(struct uart_port *port)
{
    (UART_REG(port))->intMask &= ~(TXFIFOEMT | TXFIFOTHOLD);
}

static void bcm63xx_stop_rx(struct uart_port *port)
{
    (UART_REG(port))->intMask &= ~RXFIFONE;
}

static void bcm63xx_enable_ms(struct uart_port *port)
{
}

static void bcm63xx_rx_chars(struct uart_port *port)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29)
    struct tty_struct *tty = port->info->port.tty;
#else
    struct tty_struct *tty = port->info->tty;
#endif
    unsigned int max_count = 256;
    unsigned short status;
    unsigned char ch, flag = TTY_NORMAL;

    status = (UART_REG(port))->intStatus;
    while ((status & RXFIFONE) && max_count--) {

        ch = (UART_REG(port))->Data;
        port->icount.rx++;

        /*
         * Note that the error handling code is
         * out of the main execution path
         */
        if (status & (RXBRK | RXFRAMERR | RXPARERR | RXOVFERR)) {
            if (status & RXBRK) {
                status &= ~(RXFRAMERR | RXPARERR);
                port->icount.brk++;
                if (uart_handle_break(port))
                    goto ignore_char;
            } 
            else if (status & RXPARERR)
                port->icount.parity++;
            else if (status & RXFRAMERR)
                port->icount.frame++;
            if (status & RXOVFERR)
                port->icount.overrun++;

            status &= port->read_status_mask;

            if (status & RXBRK)
                flag = TTY_BREAK;
            else if (status & RXPARERR)
                flag = TTY_PARITY;
            else if (status & RXFRAMERR)
                flag = TTY_FRAME;
        }

#ifdef SUPPORT_SYSRQ
        /*
         * Simple hack for substituting a regular ASCII char as the break
         * char for the start of the Magic Sysrq sequence.  This duplicates
         * some of the code in uart_handle_break() in serial_core.h
         */
        if (port->sysrq == 0)
        {
            if (ch == sysrq_start_char) {
                port->sysrq = jiffies + HZ*5;
                goto ignore_char;
            }
        }
#endif

        if (uart_handle_sysrq_char(port, ch))
            goto ignore_char;

        tty_insert_flip_char(tty, ch, flag);

    ignore_char:
        status = (UART_REG(port))->intStatus;
    }
    
    tty_flip_buffer_push(tty);
}

static void bcm63xx_tx_chars(struct uart_port *port)
{
    struct circ_buf *xmit = &port->info->xmit;

    (UART_REG(port))->intMask &= ~TXFIFOTHOLD;

    if (port->x_char) {
        while (!((UART_REG(port))->intStatus & TXFIFOEMT));
        /* Send character */
        (UART_REG(port))->Data = port->x_char;
        port->icount.tx++;
        port->x_char = 0;
        return;
    }
    if (uart_circ_empty(xmit) || uart_tx_stopped(port)) {
        bcm63xx_stop_tx(port);
        return;
    }

    while ((UART_REG(port))->txf_levl < port->fifosize) {
        (UART_REG(port))->Data = xmit->buf[xmit->tail];
        xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
        port->icount.tx++;
        if (uart_circ_empty(xmit))
            break;
    }

    if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
        uart_write_wakeup(port);

    if (uart_circ_empty(xmit))
        bcm63xx_stop_tx(port);
    else
        (UART_REG(port))->intMask |= TXFIFOTHOLD;
}

static void bcm63xx_modem_status(struct uart_port *port)
{
    unsigned int status;

    status = (UART_REG(port))->DeltaIP_SyncIP;

    wake_up_interruptible(&port->info->delta_msr_wait);
}

static void bcm63xx_start_tx(struct uart_port *port)
{
    if (!((UART_REG(port))->intMask & TXFIFOEMT)) {
        (UART_REG(port))->intMask |= TXFIFOEMT;
        bcm63xx_tx_chars(port);
    }
}

static irqreturn_t bcm63xx_int(int irq, void *dev_id)
{
    struct uart_port *port = dev_id;
    unsigned int status, pass_counter = BCM63XX_ISR_PASS_LIMIT;

    spin_lock(&port->lock);

    while ((status = (UART_REG(port))->intStatus & (UART_REG(port))->intMask)) {

        if (status & RXFIFONE)
            bcm63xx_rx_chars(port);

        if (status & (TXFIFOEMT | TXFIFOTHOLD))
            bcm63xx_tx_chars(port);

        if (status & DELTAIP)
            bcm63xx_modem_status(port);

        if (pass_counter-- == 0)
            break;

    }

    spin_unlock(&port->lock);

    // Clear the interrupt
    BcmHalInterruptEnable (irq);

    return IRQ_HANDLED;
}

static unsigned int bcm63xx_tx_empty(struct uart_port *port)
{
    return (UART_REG(port))->intStatus & TXFIFOEMT ? TIOCSER_TEMT : 0;
}

static unsigned int bcm63xx_get_mctrl(struct uart_port *port)
{
    return 0;
}

static void bcm63xx_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
}

static void bcm63xx_break_ctl(struct uart_port *port, int break_state)
{
}

static int bcm63xx_startup(struct uart_port *port)
{
    unsigned int rv;
    rv = BcmHalMapInterruptEx((FN_HANDLER)bcm63xx_int,
                              (unsigned int)port, port->irq,
                              "serial", INTR_REARM_NO,
                              INTR_AFFINITY_TP1_IF_POSSIBLE);
    if (rv != 0)
    {
        printk(KERN_WARNING "bcm63xx_startup: failed to register "
                            "intr %d rv=%d\n", port->irq, rv);
        return rv;
    }

    BcmHalInterruptEnable(port->irq);

    /*
     * Set TX FIFO Threshold
     */
    (UART_REG(port))->fifocfg = port->fifosize << 4;

    /*
     * Finally, enable interrupts
     */
    (UART_REG(port))->intMask = RXFIFONE;

    return 0;
}

static void bcm63xx_shutdown(struct uart_port *port)
{
    (UART_REG(port))->intMask  = 0;       
    BcmHalInterruptDisable(port->irq);
    free_irq(port->irq, port);
}

static void bcm63xx_set_termios(struct uart_port *port, 
    struct ktermios *termios, struct ktermios *old)
{
    unsigned long flags;
    unsigned int tmpVal;
    unsigned char config, control;
    unsigned int baud;

    spin_lock_irqsave(&port->lock, flags);

    /* Ask the core to calculate the divisor for us */
    baud = uart_get_baud_rate(port, termios, old, 0, port->uartclk/16); 

    (UART_REG(port))->control = 0;
    (UART_REG(port))->config = 0;

    switch (termios->c_cflag & CSIZE) {
    case CS5:
        config = BITS5SYM;
        break;
    case CS6:
        config = BITS6SYM;
        break;
    case CS7:
        config = BITS7SYM;
        break;
    case CS8:
    default:
        config = BITS8SYM;
        break;
    }
    if (termios->c_cflag & CSTOPB)
        config |= TWOSTOP;
    else
        config |= ONESTOP;

    control = 0;
    if (termios->c_cflag & PARENB) {
        control |= (TXPARITYEN | RXPARITYEN);
        if (!(termios->c_cflag & PARODD))
            control |= (TXPARITYEVEN | RXPARITYEVEN);
    }

    /*
     * Update the per-port timeout.
     */
    uart_update_timeout(port, termios->c_cflag, baud);

    port->read_status_mask = RXOVFERR;
    if (termios->c_iflag & INPCK)
        port->read_status_mask |= RXFRAMERR | RXPARERR;
    if (termios->c_iflag & (BRKINT | PARMRK))
        port->read_status_mask |= RXBRK;

    /*
     * Characters to ignore
     */
    port->ignore_status_mask = 0;
    if (termios->c_iflag & IGNPAR)
        port->ignore_status_mask |= RXFRAMERR | RXPARERR;
    if (termios->c_iflag & IGNBRK) {
        port->ignore_status_mask |= RXBRK;
        /*
         * If we're ignoring parity and break indicators,
         * ignore overruns too (for real raw support).
         */
        if (termios->c_iflag & IGNPAR)
            port->ignore_status_mask |= RXOVFERR;
    }

    /*
     * Ignore all characters if CREAD is not set.
     */
    if ((termios->c_cflag & CREAD) == 0)
        port->ignore_status_mask |= RXFIFONE;

#if 0
    if (UART_ENABLE_MS(port, termios->c_cflag))
        (UART_REG(port))->intMask = DELTAIP;
#endif

    (UART_REG(port))->control = control;
    (UART_REG(port))->config = config;

    /* Set the FIFO interrupt depth */
    (UART_REG(port))->fifoctl  =  RSTTXFIFOS | RSTRXFIFOS;

    /* 
     * Write the table value to the clock select register.
     * Value = clockFreqHz / baud / 32-1
     * take into account any necessary rounding.
     */
    tmpVal = (FPERIPH / baud) / 16;
    if( tmpVal & 0x01 )
        tmpVal /= 2;  /* Rounding up, so sub is already accounted for */
    else
        tmpVal = (tmpVal / 2) - 1; /* Rounding down so we must sub 1 */

#if !defined(CONFIG_BRCM_IKOS)
    (UART_REG(port))->baudword = tmpVal;
#endif

    /* Finally, re-enable the transmitter and receiver */
    (UART_REG(port))->control |= (BRGEN|TXEN|RXEN);

    spin_unlock_irqrestore(&port->lock, flags);
}

static const char *bcm63xx_type(struct uart_port *port)
{
    return port->type == PORT_BCM63XX ? "BCM63XX" : NULL;
}

/*
 * Release the memory region(s) being used by 'port'
 */
static void bcm63xx_release_port(struct uart_port *port)
{
}

/*
 * Request the memory region(s) being used by 'port'
 */
static int bcm63xx_request_port(struct uart_port *port)
{
    return 0;
}

/*
 * Configure/autoconfigure the port.
 */
static void bcm63xx_config_port(struct uart_port *port, int flags)
{
    if (flags & UART_CONFIG_TYPE) {
        port->type = PORT_BCM63XX;
    }
}

/*
 * verify the new serial_struct (for TIOCSSERIAL).
 */
static int bcm63xx_verify_port(struct uart_port *port, struct serial_struct *ser)
{
    return 0;
}

static struct uart_ops bcm63xx_pops = {
    .tx_empty    = bcm63xx_tx_empty,
    .set_mctrl    = bcm63xx_set_mctrl,
    .get_mctrl    = bcm63xx_get_mctrl,
    .stop_tx    = bcm63xx_stop_tx,
    .start_tx    = bcm63xx_start_tx,
    .stop_rx    = bcm63xx_stop_rx,
    .enable_ms    = bcm63xx_enable_ms,
    .break_ctl    = bcm63xx_break_ctl,
    .startup    = bcm63xx_startup,
    .shutdown    = bcm63xx_shutdown,
    .set_termios    = bcm63xx_set_termios,
    .type        = bcm63xx_type,
    .release_port    = bcm63xx_release_port,
    .request_port    = bcm63xx_request_port,
    .config_port    = bcm63xx_config_port,
    .verify_port    = bcm63xx_verify_port,
};

static struct uart_port bcm63xx_ports[] = {
    {
        .membase    = (void*)UART_BASE,
        .mapbase    = UART_BASE,
        .iotype        = SERIAL_IO_MEM,
        .irq        = INTERRUPT_ID_UART,
        .uartclk    = 14745600,
        .fifosize    = 16,
        .ops        = &bcm63xx_pops,
        .flags        = ASYNC_BOOT_AUTOCONF,
        .line        = 0,
    },
#if defined UART1_BASE
    {
        .membase    = (void*)UART1_BASE,
        .mapbase    = UART1_BASE,
        .iotype        = SERIAL_IO_MEM,
        .irq        = INTERRUPT_ID_UART1,
        .uartclk    = 14745600,
        .fifosize    = 16,
        .ops        = &bcm63xx_pops,
        .flags        = ASYNC_BOOT_AUTOCONF,
        .line        = 1,
    }
#endif
};

#ifdef CONFIG_BCM_SERIAL_CONSOLE

static void
bcm63xx_console_write(struct console *co, const char *s, unsigned int count)
{
    struct uart_port *port = &bcm63xx_ports[co->index];
    int i;
    unsigned int mask;

#if 0
    /* Wait for the TX interrupt to be disabled. */
    /* This is an indication that all pending data is sent */
    while ((UART_REG(port))->intMask & TXFIFOEMT);
#endif
// sometimes the prev loop will never end, maybe the flag won't be updated if int is disabled?

    /* wait till all pending data is sent */
    while ((UART_REG(port))->txf_levl)  {}

    /* Save the mask then disable the interrupts */
    mask = (UART_REG(port))->intMask;
    (UART_REG(port))->intMask = 0;

    for (i = 0; i < count; i++) {
        /* Send character */
        (UART_REG(port))->Data = s[i];
        /* Wait for Tx FIFO to empty */
        //while (!((UART_REG(port))->intStatus & TXFIFOEMT));
        while ((UART_REG(port))->txf_levl)    {}
        if (s[i] == '\n') {
            (UART_REG(port))->Data = '\r';
             //while (!((UART_REG(port))->intStatus & TXFIFOEMT));
            while ((UART_REG(port))->txf_levl)    {}
        }
    }

    /* Restore the mask */
    (UART_REG(port))->intMask = mask;
}

static int __init bcm63xx_console_setup(struct console *co, char *options)
{
    struct uart_port *port;
    int baud = 115200;
    int bits = 8;
    int parity = 'n';
    int flow = 'n';

    /*
     * Check whether an invalid uart number has been specified, and
     * if so, set it to port 0
     */
    if (co->index >= UART_NR)
        co->index = 0;
    port = &bcm63xx_ports[co->index];

    if (options)
        uart_parse_options(options, &baud, &parity, &bits, &flow);

    return uart_set_options(port, co, baud, parity, bits, flow);
}

struct uart_driver;
static struct uart_driver bcm63xx_reg;
static struct console bcm63xx_console = {
    .name        = "ttyS",
    .write        = bcm63xx_console_write,
//    .write        = NULL,
    .device        = uart_console_device,
    .setup        = bcm63xx_console_setup,
    .flags        = CON_PRINTBUFFER,
    .index        = -1,
    .data        = &bcm63xx_reg,
};

static int __init bcm63xx_console_init(void)
{
    register_console(&bcm63xx_console);
    return 0;
}
console_initcall(bcm63xx_console_init);

#define BCM63XX_CONSOLE    &bcm63xx_console
#else
#define BCM63XX_CONSOLE    NULL
#endif

static struct uart_driver bcm63xx_reg = {
    .owner            = THIS_MODULE,
    .driver_name    = "bcmserial",
    .dev_name        = "ttyS",
    .major            = TTY_MAJOR,
    .minor            = 64,
    .nr                = UART_NR,
    .cons            = BCM63XX_CONSOLE,
};

static int __init serial_bcm63xx_init(void)
{
    int ret;
    int i;
#if 1 /* ChenHe@MSTC for disable console output when debug flag is 0, 20130524 */
	NVRAM_DATA nvram;
	kerSysNvRamGet((char *)&nvram, sizeof(nvram), 0);
	uartDebugFlag = nvram.EngDebugFlag;
#endif

    printk(KERN_INFO "Serial: BCM63XX driver $Revision: 3.00 $\n");

#ifdef SUPPORT_SYSRQ
    printk(KERN_INFO CLRy "Magic SysRq enabled (type %c h for list "
            "of supported commands)" CLRnl, sysrq_start_char);
#endif

    ret = uart_register_driver(&bcm63xx_reg);
    if (ret >= 0) {
        for (i = 0; i < UART_NR; i++) {
            uart_add_one_port(&bcm63xx_reg, &bcm63xx_ports[i]);
        }
    } else {
        uart_unregister_driver(&bcm63xx_reg);
    }
#if 1 /* ChenHe@MSTC for disable console output when debug flag is 0, 20130524 + 20140527 */
	if (uartDebugFlag == 0){	
		uart_suspend_port(&bcm63xx_reg, &bcm63xx_ports[0]);
		uart_remove_one_port(&bcm63xx_reg, &bcm63xx_ports[0]);
	}
#endif
    return ret;
}

static void __exit serial_bcm63xx_exit(void)
{
    uart_unregister_driver(&bcm63xx_reg);
}

module_init(serial_bcm63xx_init);
module_exit(serial_bcm63xx_exit);

MODULE_DESCRIPTION("BCM63XX serial port driver $Revision: 3.00 $");
MODULE_LICENSE("GPL");
