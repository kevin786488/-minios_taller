#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel.h"

void leer_linea(const char *prompt, char *buf, int n) {
    static int sin_buffer = 0;
    if (!sin_buffer) {
        setvbuf(stdin, NULL, _IONBF, 0);
        sin_buffer = 1;
    }
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, n, stdin)) {
        buf[0] = '\0';
        return; /* EOF: feof(stdin) queda en 1 */
    }
    buf[strcspn(buf, "\r\n")] = '\0';
}

int leer_entero(const char *prompt) {
    char buf[64], *fin;
    leer_linea(prompt, buf, sizeof buf);
    if (buf[0] == '\0') return -1;
    long v = strtol(buf, &fin, 10);
    return (fin == buf || *fin != '\0') ? -1 : (int)v; /* letras o vacío -> -1 */
}

void pausar_consola(void) {
    char buf[16];
    leer_linea("\nPresione ENTER para continuar...", buf, sizeof buf);
}

void limpiar_pantalla(void) {
    printf("\033[H\033[2J");
    fflush(stdout);
}
