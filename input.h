#ifndef AGUA_INPUT_H
#define AGUA_INPUT_H
#include <stddef.h>
/* 1: linha lida; 0: EOF; -1: linha longa; -2: erro de E/S. */
int input_read_line(char *buffer, size_t capacity);
int input_parse_double(const char *text, double *value);
int input_parse_menu(const char *text, int *option);
int input_valid_measurement(double temperature, double ph, double turbidity);
#endif
