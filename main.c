#include <msp430.h> 
#include <stdint.h>

#define PWM_PERIOD      15

#define PWM_INITIAL_PW  1

/**
 * @brief Timer period
 *
 * Timer is clocked by ACLK (32768Hz).
 * We want ~32ms period, so use 1023 for CCR0
 */
#define TIMER_PERIOD        (1023)  /* ~32ms (31.25ms) */

/**
 * @brief Main function
 *
 * Demo changes PWM duty ratio on a button press
 */
int main(void)
{
    WDTCTL = WDTPW | WDTHOLD;   // Stop watchdog timer

    // initialize button
    P2DIR &= ~BIT4;             // P2.4 as in
    P2IES |= BIT4;              // falling edge
    P2IFG &= ~BIT4;             // clear ifg
    P2IE |= BIT4;               // enable interrupt

    // initialize PWM timer
    P4DIR |= BIT3;              // P4.3 out
    P4SEL |= BIT3;              // P4.3 use for TB
    TB0CCTL3 = OUTMOD_7;        // reset/set outmode
    TB0CCR0 = PWM_PERIOD;       // period
    TB0CCR3 = PWM_INITIAL_PW;   // initial pulse width value
    TB0CTL = TBSSEL__ACLK | MC__UP;

    /* initialize Timer A */
    TA0CCR0 = TIMER_PERIOD;
    TA0CCTL0 = CCIE;            // enable CCR0 interrupt
    TA0CTL = TASSEL__ACLK;

    __enable_interrupt();

    while (1);
}

/**
 * @brief TIMERA0 Interrupt service routine
 *
 * ISR debounces P2.4 and toggles LED if falling edge is detected.
 */
void __attribute__ ((interrupt(TIMER0_A0_VECTOR))) CCR0ISR (void)
{
    if ((P2IN & BIT4) == 0) // check if button is still pressed
    {
        TB0CCR3 = (TB0CCR3 + 1) & 0x0f;     // when button is pressed, increment pulse width with mod 16

        TA0CTL &= ~(MC0 | MC1); // stop and clear timer
        TA0CTL |= TACLR;
    }
    return;
}

/**
 * @brief PORT2 Interrupt service routine
 *
 * ISR starts timer which will check button press
 */
void __attribute__ ((interrupt(PORT2_VECTOR))) P2ISR (void)
{
    if ((P2IFG & BIT4) != 0)        // check if P2.4 flag is set
    {
        /* start timer */
        TA0CTL |= MC__UP;
        P2IFG &= ~BIT4;             // clear P2.4 flag
    }

    return;
}
