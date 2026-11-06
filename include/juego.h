#ifndef JUEGO_H
#define JUEGO_H

#define MAX_COLA 200

typedef struct {
    int x, y;
} Punto;

typedef struct {
    char  jugador[32];
    int   puntaje, nivel, direccion, largo;
    Punto cuerpo[MAX_COLA];
    Punto comida;
    long  guardada_en; /* time(NULL) al guardar */
} Partida;

int  guardar_partida(const Partida *p);
int  cargar_partida(const char *jugador, Partida *p);
void modo_juego(int activar);
int  tecla(int ms);
void juego_menu(void);

#endif
