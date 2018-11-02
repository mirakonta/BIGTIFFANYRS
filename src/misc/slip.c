#include <ax8052f143.h>
#include <libmftypes.h>
#include <libmfradio.h>
#include <libmfflash.h>
#include <libmfwtimer.h>
#include <libmfcrc.h>
#include "misc.h"
#include "hal.h"
#include "easyax5043.h"
#include "nbfi.h"
#include "nbfi_config.h"
#include "application.h"
#include "slip.h"
#include <stdlib.h>
#include "rf.h"

typedef struct
{
    uint8_t cmd;
    uint8_t payload[270];
    uint16_t len;
}slip_buf_t;

__no_init uint8_t __idata message @ (0xFF);

struct wtimer_desc __xdata slip_process_desc;
struct wtimer_desc __xdata uart_sleep_timer;

static __xdata char uart_can_sleep = 1;

__xdata slip_buf_t slip_rxbuf;

#ifdef TEXT_MODE
uint8_t uart_mode = UART_MODE_TEXT;
#else
uint8_t uart_mode = UART_MODE_SLIP;
#endif // TEXT_MODE


static uint8_t SLIP_Receive();

/*void SLIP_test_unsleep()
{
    DIRA &= ~(1<<2);
    PORTA_2 = HIGH;
    INTCHGA = 1 << 2;
    INTCHGB = 0;
    INTCHGC = 0;
    //do { char tmp_pinA = PINA; } while(0);
    IE_3 = 1;
    EA = 1;
    if (!PINA_2) {
        uart_can_sleep = 0;
        IE_3 = 0;
    }
    do { char tmp_pinA = PINA; } while(0);
}*/

char SLIP_isCanSleep()
{
    return uart_can_sleep;
}

void SLIP_setCanSleep(char c)
{
    uart_can_sleep = c;
    #ifndef AMPER
    if(c)
    {
        wtimer0_remove(&slip_process_desc);
    }
    #endif
}

void SLIP_toCanSleep(struct wtimer_desc __xdata *desc)
{
    if(rf_state == STATE_OFF)
    {
        SLIP_setCanSleep(1);
    }
    else SLIP_Wait_Before_Sleep();
}

static void SLIP_restartTimerSleep()
{
    //wtimer0_remove(&uart_sleep_timer);
    SLIP_Wait_Before_Sleep();
    ScheduleTask(&slip_process_desc, SLIP_Process, RELATIVE, MILLISECONDS(7));
    #ifndef AMPER
    SLIP_Init();
    #endif
}

void SLIP_Init()
{
    wtimer0_remove(&uart_sleep_timer);
    uart_can_sleep = 0;
    ScheduleTask(&slip_process_desc, SLIP_Process, RELATIVE, MILLISECONDS(7));
}

void SLIP_Send_debug(uint8_t __generic * str, uint8_t len)
{
    SLIP_restartTimerSleep();
    for(int i=0; i<len; i++)
    {
        if(str[i]) uart1_tx(str[i]);
        else break;

    }
    uart1_tx(0x0d);
    SLIP_restartTimerSleep();
}

void SLIP_Send(uint8_t cmd, uint8_t __generic * payload, uint8_t len)
{

    SLIP_restartTimerSleep();
    //UART_Init();
    // Encode to SLIP
    uart1_tx(SLIP_START);
    uart1_tx(cmd);
    for(int i=0; i<=len; i++)
    {
        uint8_t c;
        if(i == len) c = CRC8(payload, len);
        else c = payload[i];
        if((c==SLIP_START) || (c==SLIP_END) || (c==SLIP_ESC))
        {
            uart1_tx(SLIP_ESC);
            uart1_tx(c ^ 0xFF);
        }
        else uart1_tx(c);
    }
    uart1_tx(SLIP_END);
    SLIP_restartTimerSleep();
}

//static __xdata nbfi_packet_t pkt;
nbfi_transport_packet_t __xdata* packet;
//uint32_t aaa;

void SLIP_Process(struct wtimer_desc __xdata *desc)
{

    static uint8_t __xdata st = 0;
    if(SLIP_Receive())
    {
        #ifdef AMPER_2_3
        if(nbfi.mode <= DRX)
        {
            if((slip_rxbuf.cmd != SLIP_CMD_SET_RX)&&(slip_rxbuf.cmd != SLIP_NBFI_SETTINGS)) return;
        }
        #endif
        switch(slip_rxbuf.cmd)
        {
        case SLIP_CMD_ECHO:
            SLIP_Send(slip_rxbuf.cmd, slip_rxbuf.payload, slip_rxbuf.len);
            break;
        case SLIP_CMD_SEND_DL:
            packet =  NBFi_AllocateTxPkt(slip_rxbuf.len - 1);
            if(packet)
            {
                memcpy_xdata(&packet->phy_data.header, slip_rxbuf.payload, slip_rxbuf.len);
                packet->phy_data_length = slip_rxbuf.len - 1;
                packet->handshake = HANDSHAKE_NONE;
                packet->state = PACKET_QUEUED;
                nbfi.mode = TRANSPARENT;
                NBFi_Force_process();
                //NBFi_Wait_For_Transmit_End();
                slip_rxbuf.payload[0] = 0;      //OK
            }
            else slip_rxbuf.payload[0] = 0x04;  //ERR_BUFFER_FULL
            SLIP_Send(slip_rxbuf.cmd, slip_rxbuf.payload, 1);
            break;
        case SLIP_CMD_GET_PACKETS_TO_SEND:
            slip_rxbuf.payload[0] = NBFi_Packets_To_Send();
            SLIP_Send(slip_rxbuf.cmd, slip_rxbuf.payload, 1);
            break;
        case SLIP_CMD_SET_FREQUENCY:
            if(slip_rxbuf.len == 4)
            {
                uint32_t freq = 0;
                freq = (freq+slip_rxbuf.payload[0])<<8;
                freq = (freq+slip_rxbuf.payload[1])<<8;
                freq = (freq+slip_rxbuf.payload[2])<<8;
                freq = (freq+slip_rxbuf.payload[3]);
                nbfi.tx_freq = freq;
            }
            SLIP_Send(slip_rxbuf.cmd, slip_rxbuf.payload, 1);
            break;
        case SLIP_CMD_SET_POWER:
            if(slip_rxbuf.len == 1)
            {
                nbfi.tx_pwr = slip_rxbuf.payload[0];
            }
            slip_rxbuf.payload[0] = nbfi.tx_pwr;
            SLIP_Send(slip_rxbuf.cmd, slip_rxbuf.payload, 1);
            break;
        case SLIP_CMD_SEND_CORR_PCKT:
            NBFi_TX_Correction();
            slip_rxbuf.payload[0] = 0;
            SLIP_Send(slip_rxbuf.cmd, slip_rxbuf.payload, 1);
            break;
        case SLIP_CMD_SET_DL_ADDRESS:
            if(slip_rxbuf.len == 3)
            {
                memcpy_xdata(nbfi.dl_ID, slip_rxbuf.payload, sizeof(nbfi.dl_ID));
            }
            SLIP_Send(slip_rxbuf.cmd, nbfi.dl_ID, sizeof(nbfi.dl_ID));
            break;
        case SLIP_CMD_GET_SERIAL:
            slip_rxbuf.payload[0] = nbfi.full_ID[0];
            slip_rxbuf.payload[1] = nbfi.full_ID[1];
            slip_rxbuf.payload[2] = nbfi.full_ID[2];
            slip_rxbuf.payload[3] = nbfi.full_ID[3];
            SLIP_Send(slip_rxbuf.cmd, slip_rxbuf.payload, 4);
            break;
        case SLIP_CMD_SOFTWARE_RESET:
            message = 0xB1;
            PCON |= 0x10; // SOFTWARE RESET
            break;
        case SLIP_CMD_NBFI_TX_UL:
            slip_rxbuf.payload[0] = NBFi_Send((uint8_t __generic*) slip_rxbuf.payload, slip_rxbuf.len);
            SLIP_Send(slip_rxbuf.cmd, slip_rxbuf.payload, 1);
            break;
        case SLIP_NBFI_CONFIG:
            if(NBFi_Config_Parser(slip_rxbuf.payload))
            {
                nbfi_settings_changed = 0;
                SLIP_Send(slip_rxbuf.cmd, slip_rxbuf.payload, 7);
            }
            break;
        case SLIP_CMD_SET_RX :
            if(slip_rxbuf.len == 1)
            {
                if((slip_rxbuf.payload[0]&0x80) && (nbfi.tx_phy_channel != UL_PSK_FASTDL)) NBFi_Config_Set_FastDl(1);
                else if(!(slip_rxbuf.payload[0]&0x80) && (nbfi.tx_phy_channel == UL_PSK_FASTDL)) NBFi_Config_Set_FastDl(0);
                if((slip_rxbuf.payload[0]&0x80) == 0)
                {
                    SLIP_setCanSleep(slip_rxbuf.payload[0] <= DRX);
                    NBFi_Go_To_Sleep(slip_rxbuf.payload[0] <= DRX);
                }
            }
            slip_rxbuf.payload[0] = nbfi.mode + ((nbfi.tx_phy_channel == UL_PSK_FASTDL)?0x80:0);
            SLIP_Send(slip_rxbuf.cmd, slip_rxbuf.payload, 1);
            break;
        case SLIP_CMD_SEND_FAST_DL:
            NBFi_Config_Set_FastDl(true);
            NBFi_Clear_TX_Buffer();
            //nbfi.mode = TRANSPARENT;
            NBFi_Send((uint8_t __generic*) slip_rxbuf.payload, slip_rxbuf.len);
            break;
        case SLIP_NBFI_SETTINGS:
            if(slip_rxbuf.len)
            {
                if(slip_rxbuf.len > sizeof(nbfi_settings_t)) slip_rxbuf.len = sizeof(nbfi_settings_t);
                NBFi_Clear_TX_Buffer();
                for(uint8_t i = 0; i != sizeof(nbfi_settings_t); i++)
                {
                    ((uint8_t __xdata*)&nbfi)[i] = slip_rxbuf.payload[i];
                }
                slip_rxbuf.payload[0] = 0;
                if(rf_state == STATE_RX) NBFi_RX();
                SLIP_Send(slip_rxbuf.cmd, slip_rxbuf.payload, 1);
            }
            else
            {
                SLIP_Send(slip_rxbuf.cmd, (uint8_t __xdata*)&nbfi, sizeof(nbfi_settings_t));
            }
            break;
            case SLIP_NBFI_QUALITY:
                    SLIP_Send(slip_rxbuf.cmd, (uint8_t __xdata*)&nbfi_state, sizeof(nbfi_state_t));
            break;
#ifdef TEXT_MODE
        case SLIP_TEXT_RECEIVED:
            NBFi_Send((uint8_t __generic*) slip_rxbuf.payload, slip_rxbuf.len);
            slip_rxbuf.len = 0;
            break;

#endif
        default:
            break;
        }
    }
    if(desc && !uart_can_sleep) ScheduleTask(desc, 0, RELATIVE, MILLISECONDS(7));

}

static uint8_t SLIP_Receive()
{
    char c;
    static uint8_t mode = SLIP_MODE_START;
    char restart_sleep = 1;

    while(uart1_rxcount())
    {
        SLIP_Wait_Before_Sleep();
        if (restart_sleep) {
            restart_sleep = 0;
            SLIP_restartTimerSleep();
        }
        c = uart1_rx();
#ifdef TEXT_MODE
if(uart_mode == UART_MODE_TEXT)
{
        if((c == 0x0A) || (c == 0x0D))
        {
            if(slip_rxbuf.len)
            {
                slip_rxbuf.cmd = SLIP_TEXT_RECEIVED;
                return 1;
            }
        }
        else
        {
            slip_rxbuf.payload[slip_rxbuf.len++] = c;
        }
}
else
#else
        switch (mode)
        {
        case  SLIP_MODE_START:
            if(c == SLIP_START)
            {
                slip_rxbuf.len = 0;
                memset_xdata(slip_rxbuf.payload, 0, 20);
                mode = SLIP_MODE_RECEIVING_CMD;
            }
            break;
        case SLIP_MODE_RECEIVING_PAYLOAD:
        case SLIP_MODE_RECEIVING_CMD:
            if(c == SLIP_ESC)
            {
                mode = SLIP_MODE_ESC;
                break;
            }
            if(c == SLIP_START)
            {
                slip_rxbuf.len = 0;
                memset_xdata(slip_rxbuf.payload, 0, 20);
                mode = SLIP_MODE_RECEIVING_CMD;
                break;
            }
            if(c == SLIP_END)
            {

                mode = SLIP_MODE_START;
                if(slip_rxbuf.len) slip_rxbuf.len--;
                else
                {
                    //slip_rxbuf.len ++;
                }
                if(CRC8(((uint8_t __generic*)&slip_rxbuf.payload[0]), slip_rxbuf.len) != slip_rxbuf.payload[slip_rxbuf.len]) return 0;
                return 1;
            }
            if(mode == SLIP_MODE_RECEIVING_CMD)
            {
                slip_rxbuf.cmd = c;
                mode = SLIP_MODE_RECEIVING_PAYLOAD;
                break;
            }
            if(mode == SLIP_MODE_RECEIVING_PAYLOAD)
            {
                slip_rxbuf.payload[slip_rxbuf.len++] = c;
                break;
            }
            break;
        case SLIP_MODE_ESC:

            slip_rxbuf.payload[slip_rxbuf.len++] = c ^ 0xFF;
            mode = SLIP_MODE_RECEIVING_PAYLOAD;
            break;
        default:
            mode = SLIP_MODE_START;
            break;
        }
#endif
        if(slip_rxbuf.len == 270)
        {
            mode = SLIP_MODE_START;
        }
    }
    return 0;
}

void SLIP_Wait_Before_Sleep()
{
    uart_can_sleep = 0;
    if(nbfi.mode <= DRX)
    {
        ScheduleTask(&uart_sleep_timer, SLIP_toCanSleep, RELATIVE, MILLISECONDS(100));
        //SLIP_Init();
    }
}

void SLIP_Check_For_Correct_State()
{
        if(nbfi.mode <= DRX )
        {
            if(!SLIP_isCanSleep())
            {
                SLIP_Process(0);
                #ifndef AMPER_2_3
                if (!CheckTask(&uart_sleep_timer)) SLIP_Wait_Before_Sleep();
                #endif // AMPER_2_3

            }
            else
            {
                if(rf_state != STATE_OFF)
                {
                    SLIP_Wait_Before_Sleep();
                    SLIP_Process(0);
                }
            }
        }
}

