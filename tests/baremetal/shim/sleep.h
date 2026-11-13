// SIMULACION EN PC: sustituye a sleep.h (el tiempo avanza en el modelo del TRNG).
#pragma once
void mock_usleep(unsigned us);
#define usleep(us) mock_usleep(us)
