// Scope trigger pins, driven high while the corresponding task runs

#pragma once

#ifdef OPTION_IO

void ioInit(void);

void setPin1(void); // fastTask
void rstPin1(void);
void setPin2(void); // slowTask
void rstPin2(void);

#endif
