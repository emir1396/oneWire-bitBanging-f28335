/*
 *  ONE-WIRE bitbanging
 *  testiranje emulacije one-wire interfejsa sa jednim pinom(GPIO 32)
 *  koristeci timer1 i xint3
 *  datasheet https://datasheets.maximintegrated.com/en/ds/DS18B20.pdf
 */

#include "DSP2833x_Device.h"
#include "DSP2833x_Examples.h"


//GPIO32 u otput mode
#define ONEWIRE_OUT                                 \
            EALLOW  ;                               \
            GpioCtrlRegs.GPBDIR.bit.GPIO32 = 1;     \
            EDIS;                                   \
//GPIO32 u high-z
#define ONEWIRE_HIGHZ                               \
            EALLOW  ;                               \
            GpioCtrlRegs.GPBDIR.bit.GPIO32 = 0;     \
            EDIS;                                   \
//GPIO32 LOW
#define ONEWIRE_LOW                                 \
            GpioDataRegs.GPBCLEAR.bit.GPIO32 = 1;   \
//GPIO32 IN
#define ONEWIRE_READ                                \
            GpioDataRegs.GPBDAT.bit.GPIO32          \
//GPIO32 HIGH
#define ONEWIRE_HIGH                                \
            GpioDataRegs.GPBSET.bit.GPIO32 = 1;     \
//stop timer1
#define TIMER1_STOP                                 \
            CpuTimer1Regs.TCR.bit.TSS = 1;          \
//startuje timer1
#define TIMER1_START                                \
            CpuTimer1Regs.TCR.bit.TSS = 0;          \
//reloaduje timer1
#define TIMER1_RELOAD                               \
            CpuTimer1Regs.TCR.bit.TRB = 1;          \
//setuje i reloaduje timer1(uS)
#define TIMER1_SET(num)                             \
            CpuTimer1Regs.PRD.all = 150*num;        \
            CpuTimer1Regs.TCR.bit.TRB = 1;          \
//payload MASK
#define PAYLOAD_MASK 0x0001;

Uint16 start = 0, getTemp = 0, convertTemp = 0,updateTemp = 0, doNext = 1;
Uint64 payload = 0x0000000000000000;
Uint16 bytesExpected = 0x0000;
Uint16 semafor = 0;
Uint16 readBufferIndex = 0;
Uint16 readBuffer[10] = {0,0,0,0,0,0,0,0,0,0};
float32 temperature = 0;
//
extern void InitSysCtrl(void);
extern void InitPieCtrl(void);
extern void InitPieVectTable(void);
//
void GpioSelect();
void startOneWire();
void convertToCelsius();
interrupt void xint3_isr();
interrupt void cpu_timer1_isr();

int main(void)
{


    InitSysCtrl();                                                      //Disables watchdog and initializes clocks
    GpioSelect();                                                       //Configures pins(all input, all gpio)

    //Setting up the external interrupt XINT3
    EALLOW;
    GpioIntRegs.GPIOXINT3SEL.bit.GPIOSEL = 0;                           //GPIO32 as input for XINT3
    EDIS;
    XIntruptRegs.XINT3CR.bit.ENABLE = 0;                                //interrupt disabled
    XIntruptRegs.XINT3CR.bit.POLARITY = 0;                              //falling edge triggered

    //Setting up the timer2
    CpuTimer1Regs.PRD.all = 72000;                                      //set the period to 480uS (150 * 480)
    CpuTimer1Regs.TPR.all = 0;                                          //prescaler set to 1 -> timerClock = SYSCLKOUT
    CpuTimer1Regs.TPRH.all = 0;
    CpuTimer1Regs.TCR.bit.TSS = 1;                                      //stop the timer
    CpuTimer1Regs.TCR.bit.TRB = 1;                                      //reload timer (TIMH:TIM = PRD)
    //ovaj stop se odnosi na emulatorski breakpoint!!
    CpuTimer1Regs.TCR.bit.SOFT = 0;                                     //soft stop (stops when TIMH:TIM == 0)
    CpuTimer1Regs.TCR.bit.FREE = 0;                                     //soft stop
    CpuTimer1Regs.TCR.bit.TIE = 0;                                      //disable the interrupt
    CpuTimer1.InterruptCount = 0;
    //Setting up interrupts
    InitPieCtrl();
    IER = 0x0000;                                                       //Disable all CPU interrupts
    IFR = 0x0000;                                                       //Clear all CPU intr flags
    //neki problemchich sa linkovanjem, cant be bothered
    //InitPieVectTable();                                               //Fills the ivt with default isr(vaj no worky?)
        PieCtrlRegs.PIECTRL.bit.ENPIE = 1;
    EALLOW;                                                             //Remaping the ivt
    PieVectTable.XINT3 = &xint3_isr;
    PieVectTable.XINT13 = &cpu_timer1_isr;
    EDIS;
    PieCtrlRegs.PIEIER12.bit.INTx1 = 1;                                 //enable PIE 12.1 (xint3)
    IER |= M_INT12;                                                      //enable CPU int 12(xint3)
    IER |= M_INT13;                                                     //enable CPU int 13(cpu timer1)



    EINT;                                                               //enable global interrupt INTM
    ERTM;                                                               //enable global realtime interrupt DBGM

    while (1){
        if (start){
            startOneWire();
            payload = 0x0000017F55554ECC;
            bytesExpected = 0;
            start = 0;
        }
        if (convertTemp){
            startOneWire();
            //stavi se 1 pre 44 jer ne bi poslao ceo bajt poslednje cetvorke
            payload = 0x144CC;
            bytesExpected = 9;
            getTemp = 1;
            convertTemp = 0;
        }
        if (getTemp && doNext){
            startOneWire();
            payload = 0xBECC;
            bytesExpected = 9;
            updateTemp = 1;
            getTemp = 0;
        }
        if (updateTemp && doNext){
            convertToCelsius();
            updateTemp = 0;
        }

    }
}

void startOneWire(){
    doNext = 0;
    Uint16 i=0;
    for (i = 0; i < 10; i++){
        readBuffer[i] = 0x0000;
    }
    EALLOW;
    GpioCtrlRegs.GPBDIR.bit.GPIO32 = 1;                                 //GPIO32 as output
    EDIS;
    //set the timer to 480uS
    CpuTimer1Regs.PRD.all = 72000;                                      //set the period to 480uS (150 * 480)
    CpuTimer1Regs.TCR.bit.TRB = 1;                                      //reload timer (TIMH:TIM = PRD)
    CpuTimer1Regs.TCR.bit.TIF = 1;                                      //clear the flag
    CpuTimer1Regs.TCR.bit.TIE = 1;                                      //enable the interrupt
    CpuTimer1Regs.TCR.bit.TSS = 1;                                      //stop the timer

    //pull the bus LOW
    GpioDataRegs.GPBCLEAR.bit.GPIO32 = 1;
    //start the timer
    CpuTimer1Regs.TCR.bit.TSS = 0;                                      //start the timer
}

void GpioSelect(){
    EALLOW;
    //All pins as INPUT
    GpioCtrlRegs.GPADIR.all = 0;
    GpioCtrlRegs.GPBDIR.all = 0;
    GpioCtrlRegs.GPCDIR.all = 0;
    //All pins as GPIO
    GpioCtrlRegs.GPAMUX1.all = 0;
    GpioCtrlRegs.GPAMUX2.all = 0;
    GpioCtrlRegs.GPBMUX1.all = 0;
    GpioCtrlRegs.GPBMUX2.all = 0;
    GpioCtrlRegs.GPCMUX1.all = 0;
    GpioCtrlRegs.GPCMUX2.all = 0;
    //
    GpioCtrlRegs.GPBDIR.bit.GPIO32 = 1;                                 //GPIO32 as output
    GpioCtrlRegs.GPBPUD.bit.GPIO32 = 1;                                 //GPIO32 disable pullup
    GpioDataRegs.GPBSET.bit.GPIO32 = 1;                                 //GPIO32=HIGH, oneWire idle mode

    EDIS;
}

interrupt void xint3_isr(){
    //falling edge detected on GPIO32
    semafor++;
    XIntruptRegs.XINT3CR.bit.ENABLE = 0;                                //disable this interrupt
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP12;
}

interrupt void cpu_timer1_isr(){
    switch (semafor) {
        case 0:
            //poslat RESET
            //staviti pin u HIGH-Z
            ONEWIRE_HIGHZ;
            XIntruptRegs.XINT3CR.bit.ENABLE = 1;                                //enable xint3
            if (CpuTimer1.InterruptCount++){
                //nema senzora na magistrali
                CpuTimer1.InterruptCount = 0;
                XIntruptRegs.XINT3CR.bit.ENABLE = 0;                                //enable xint3
                TIMER1_STOP;
            }
            break;
        case 1:
            CpuTimer1.InterruptCount = 0;
            ONEWIRE_HIGHZ;
            TIMER1_STOP;
            TIMER1_SET(2);
            if (payload || bytesExpected){
            //ako saljem ili primam
                semafor++;
            }else{
            //none
                semafor = 6;
            }
            TIMER1_START;
                    break;
        case 2:
            //pull the line LOW
            ONEWIRE_OUT;
            ONEWIRE_LOW;
            TIMER1_STOP;
            TIMER1_SET(2);
            if(payload){
                semafor++;
            }
            else if(bytesExpected){
                semafor = 4;
            } else {
                semafor = 1;
            }

            TIMER1_START;
                    break;
        case 3:
            TIMER1_STOP;
            //mogu samo iz 2 da dodjem tako da je pin vec OUT i LOW
            //maskiram MSB
            GpioDataRegs.GPBSET.bit.GPIO32 = payload & PAYLOAD_MASK;
            payload>>=1;
            //takvo stanje mora da se ostavi na 60uS
            TIMER1_SET(60);
            TIMER1_START;
            semafor = 1;
                    break;
        case 4:
            //mogu samo iz 2 da dodjem tako da moram da pin stavim u HIGHZ
            TIMER1_STOP;
            ONEWIRE_HIGHZ;
            //ostavim vremena slave-u da postavi nivo na magistralu
            TIMER1_SET(8);
            TIMER1_START;
            semafor++;
                    break;
        case 5:
            //citanje i upis podatka
            readBuffer[bytesExpected] |= (ONEWIRE_READ << readBufferIndex++);
            if (readBufferIndex == 8){
                readBufferIndex = 0;
                bytesExpected--;
            }
            TIMER1_SET(60);
            TIMER1_START;
            semafor = 1;
                    break;
        case 6:
            if (CpuTimer1.InterruptCount++){
            //nema ni payloada ni bytesExpecteda 500mS
            //kraj
                CpuTimer1.InterruptCount = 0;
                TIMER1_STOP;
                semafor=0;
                doNext = 1;
                break;
            }
            TIMER1_STOP;
            TIMER1_SET(500000);
            if (payload || bytesExpected){
                semafor = 1;
            }
            TIMER1_START;
                    break;
        default:
            break;
    }
    asm(" NOP");
}

void convertToCelsius(){
    //dodati konverziju negativnih, broj je u komplementu dvojke
    temperature = (readBuffer[9] & 0xF0)>>4 | (readBuffer[8] & 0x0F)<<4;
    if(readBuffer[9] & 0x0001) temperature += 0.06250f;
    if(readBuffer[9] & 0x0002) temperature += 0.12500f;
    if(readBuffer[9] & 0x0004) temperature += 0.25000f;
    if(readBuffer[9] & 0x0008) temperature += 0.50000f;

}

