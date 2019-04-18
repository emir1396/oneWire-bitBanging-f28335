# oneWire-bitBanging-f28335
bitBanging of the Dallas one-wire interface on TI's *TMS320F28335* using only a timer and an external interrupt.
External interrupt is used to determine the presence of the slave on the line and the timer is used for everything else.
Tested with DALLAS18B20 temperature sensor(convertTemp = 1) to initiate conversion and reading. 
