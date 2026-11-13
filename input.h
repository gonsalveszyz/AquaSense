#ifndef AGUA_INPUT_H
#define AGUA_INPUT_H
#include <stddef.h>
/* 1: linha lida; 0: EOF; -1: linha longa ou com NUL; -2: erro de E/S.
 * Linhas invalidas sao consumidas por inteiro antes de retornar. */
int input_read_line(char *buffer, size_t capacity);
int input_parse_double(const char *text, double *value);
int input_parse_menu(const char *text, int *option);
int input_valid_measurement(double temperature, double ph, double turbidity);
#endif
