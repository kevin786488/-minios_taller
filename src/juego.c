#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <time.h>
#include <sys/select.h>
#include "kernel.h"
#include "juego.h"
#include "fs.h"
#include "seguridad.h"

#define ANCHO_TAB 30
#define ALTO_TAB  14

#define DIR_ARRIBA    0
#define DIR_DERECHA   1
#define DIR_ABAJO     2
#define DIR_IZQUIERDA 3

static struct termios original;
static int term_guardada = 0;
static volatile sig_atomic_t flag_autoguardar = 0;

static void manejador_sigint_juego(int sig) {
    (void)sig;
    flag_autoguardar = 1;
}

int guardar_partida(const Partida *p) {
    char ruta[64];
    snprintf(ruta, sizeof ruta, DIR_DATOS "partida_%s.sav", p->jugador);
    return fs_guardar_bloque(ruta, p, sizeof *p);
}

int cargar_partida(const char *jugador, Partida *p) {
    char ruta[64];
    snprintf(ruta, sizeof ruta, DIR_DATOS "partida_%s.sav", jugador);
    if (fs_cargar_bloque(ruta, p, sizeof *p) != 0) {
        return -1; /* no tiene partida guardada */
    }
    if (!seg_verificar_propiedad(p->jugador)) {
        return -1;
    }
    return 0;
}

void modo_juego(int activar) {
    if (!isatty(STDIN_FILENO)) return;
    if (activar) {
        struct termios t;
        if (tcgetattr(STDIN_FILENO, &original) == 0) {
            term_guardada = 1;
            t = original;
            t.c_lflag &= (tcflag_t)~(ICANON | ECHO); /* sin esperar Enter y sin eco */
            tcsetattr(STDIN_FILENO, TCSANOW, &t);
        }
    } else if (term_guardada) {
        tcsetattr(STDIN_FILENO, TCSANOW, &original); /* SIEMPRE restaurar */
    }
}

/* Devuelve la tecla presionada, o -1 si no se presionó nada en 'ms' milisegundos */
int tecla(int ms) {
    fd_set fds;
    struct timeval tv = { 0, ms * 1000 };
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
        char c;
        if (read(STDIN_FILENO, &c, 1) == 1) return (unsigned char)c;
    }
    return -1;
}

static int ocupa_serpiente(const Partida *p, int x, int y) {
    for (int i = 0; i < p->largo; i++) {
        if (p->cuerpo[i].x == x && p->cuerpo[i].y == y) return 1;
    }
    return 0;
}

static void generar_comida(Partida *p) {
    for (int intentos = 0; intentos < 500; intentos++) {
        int cx = 1 + (rand() % (ANCHO_TAB - 2));
        int cy = 1 + (rand() % (ALTO_TAB - 2));
        if (!ocupa_serpiente(p, cx, cy)) {
            p->comida.x = cx;
            p->comida.y = cy;
            return;
        }
    }
    p->comida.x = 2;
    p->comida.y = 2;
}

static void inicializar_partida(Partida *p, const char *jugador) {
    memset(p, 0, sizeof *p);
    strncpy(p->jugador, jugador, sizeof p->jugador - 1);
    p->puntaje = 0;
    p->nivel = 1;
    p->direccion = DIR_DERECHA;
    p->largo = 4;

    int cx = ANCHO_TAB / 2;
    int cy = ALTO_TAB / 2;
    for (int i = 0; i < p->largo; i++) {
        p->cuerpo[i].x = cx - i;
        p->cuerpo[i].y = cy;
    }
    srand((unsigned int)time(NULL));
    /* Posición inicial determinista de comida cercana para facilitar pruebas */
    p->comida.x = cx + 2;
    p->comida.y = cy;
    p->guardada_en = (long)time(NULL);
}

static void dibujar_tablero(const Partida *p, const char *aviso) {
    if (isatty(STDOUT_FILENO)) {
        printf("\033[H\033[J");
    }
    printf("=== MINIOS SNAKE v3.0 | Jugador: %s | Puntaje: %d | Nivel: %d | Largo: %d ===\n",
           p->jugador, p->puntaje, p->nivel, p->largo);
    printf("Cabeza: (%d,%d) | Comida: (%d,%d)\n",
           p->cuerpo[0].x, p->cuerpo[0].y, p->comida.x, p->comida.y);

    for (int y = 0; y < ALTO_TAB; y++) {
        for (int x = 0; x < ANCHO_TAB; x++) {
            if (y == 0 || y == ALTO_TAB - 1 || x == 0 || x == ANCHO_TAB - 1) {
                putchar('#');
            } else if (p->cuerpo[0].x == x && p->cuerpo[0].y == y) {
                putchar('@');
            } else if (ocupa_serpiente(p, x, y)) {
                putchar('o');
            } else if (p->comida.x == x && p->comida.y == y) {
                putchar('*');
            } else {
                putchar(' ');
            }
        }
        putchar('\n');
    }
    printf("Controles: [W/A/S/D] Mover | [G] Guardar | [Q] Guardar y salir | [X] Terminar y registrar récord | [Ctrl+C] Autoguardar\n");
    if (aviso && aviso[0] != '\0') {
        printf(">>> %s\n", aviso);
    }
    fflush(stdout);
}

static void ejecutar_bucle_snake(Partida *p) {
    flag_autoguardar = 0;
    void (*prev_sigint)(int) = signal(SIGINT, manejador_sigint_juego);

    modo_juego(1);
    char aviso[128] = "";
    snprintf(aviso, sizeof aviso, "Sesión activa para '%s' (Puntaje: %d)", p->jugador, p->puntaje);
    dibujar_tablero(p, aviso);

    int en_curso = 1;
    while (en_curso) {
        int delay_ms = 180 - (p->nivel - 1) * 15;
        if (delay_ms < 70) delay_ms = 70;

        int c = tecla(delay_ms);

        if (flag_autoguardar) {
            p->guardada_en = (long)time(NULL);
            guardar_partida(p);
            modo_juego(0);
            signal(SIGINT, prev_sigint);
            printf("\n  [AUTOGUARDADO Ctrl+C] Partida de '%s' guardada en datos/partida_%s.sav (Puntaje: %d).\n",
                   p->jugador, p->jugador, p->puntaje);
            fs_log_evento("INFO", "Partida autoguardada por señal SIGINT (Ctrl+C)");
            return;
        }

        if (c == 'w' || c == 'W') {
            if (p->direccion != DIR_ABAJO) p->direccion = DIR_ARRIBA;
        } else if (c == 's' || c == 'S') {
            if (p->direccion != DIR_ARRIBA) p->direccion = DIR_ABAJO;
        } else if (c == 'a' || c == 'A') {
            if (p->direccion != DIR_DERECHA) p->direccion = DIR_IZQUIERDA;
        } else if (c == 'd' || c == 'D') {
            if (p->direccion != DIR_IZQUIERDA) p->direccion = DIR_DERECHA;
        } else if (c == 'g' || c == 'G') {
            if (seg_verificar(OBJ_PARTIDAS, D_UPDATE)) {
                p->guardada_en = (long)time(NULL);
                if (guardar_partida(p) == 0) {
                    snprintf(aviso, sizeof aviso, "Partida guardada en datos/partida_%s.sav (Puntaje: %d)",
                             p->jugador, p->puntaje);
                    fs_log_evento("INFO", "Partida guardada manualmente");
                }
            }
            dibujar_tablero(p, aviso);
            continue;
        } else if (c == 'q' || c == 'Q') {
            if (seg_verificar(OBJ_PARTIDAS, D_UPDATE)) {
                p->guardada_en = (long)time(NULL);
                guardar_partida(p);
            }
            modo_juego(0);
            signal(SIGINT, prev_sigint);
            printf("\n  [GUARDADO] Sesión guardada en datos/partida_%s.sav (Puntaje: %d, Nivel: %d).\n",
                   p->jugador, p->puntaje, p->nivel);
            fs_log_evento("INFO", "Salida de partida con guardado en disco");
            return;
        } else if (c == 'x' || c == 'X') {
            /* Terminar partida voluntariamente y guardar récord en disco virtual */
            en_curso = 0;
            break;
        }

        /* Si la entrada no es un terminal interactivo y llegó EOF sin más teclas, salir limpiamente */
        if (!isatty(STDIN_FILENO) && c == -1) {
            break;
        }

        /* Calcular nueva posición de la cabeza */
        Punto nueva = p->cuerpo[0];
        if (p->direccion == DIR_ARRIBA)    nueva.y--;
        if (p->direccion == DIR_DERECHA)   nueva.x++;
        if (p->direccion == DIR_ABAJO)     nueva.y++;
        if (p->direccion == DIR_IZQUIERDA) nueva.x--;

        /* Colisión con paredes */
        if (nueva.x <= 0 || nueva.x >= ANCHO_TAB - 1 ||
            nueva.y <= 0 || nueva.y >= ALTO_TAB - 1) {
            en_curso = 0;
            break;
        }

        /* Colisión con su propio cuerpo */
        int choca_cuerpo = 0;
        for (int i = 0; i < p->largo - 1; i++) {
            if (p->cuerpo[i].x == nueva.x && p->cuerpo[i].y == nueva.y) {
                choca_cuerpo = 1;
                break;
            }
        }
        if (choca_cuerpo) {
            en_curso = 0;
            break;
        }

        int comio = (nueva.x == p->comida.x && nueva.y == p->comida.y);
        if (comio && p->largo < MAX_COLA) {
            p->largo++;
            p->puntaje += 10;
            p->nivel = 1 + (p->puntaje / 30);
        }

        for (int i = p->largo - 1; i > 0; i--) {
            p->cuerpo[i] = p->cuerpo[i - 1];
        }
        p->cuerpo[0] = nueva;

        if (comio) {
            generar_comida(p);
            snprintf(aviso, sizeof aviso, "¡Comida alcanzada! Puntaje: %d | Nivel: %d", p->puntaje, p->nivel);
        }

        dibujar_tablero(p, aviso);
    }

    modo_juego(0);
    signal(SIGINT, prev_sigint);

    printf("\n==================== FIN DE LA PARTIDA ====================\n");
    printf(" Jugador: %s | Puntaje final: %d | Nivel alcanzado: %d\n",
           p->jugador, p->puntaje, p->nivel);
    int id_rec = fs_registrar_record(p->jugador, p->puntaje, p->nivel);
    if (id_rec > 0) {
        printf(" [RÉCORD] Puntaje guardado en el disco virtual (minios.disk) con ID=%d.\n", id_rec);
        fs_log_evento("INFO", "Puntaje de partida registrado en disco virtual");
    }
    printf("===========================================================\n");
}

void juego_menu(void) {
    while (1) {
        printf("\n========= VIDEOJUEGO MINIOS SNAKE (SESIÓN EN DISCO) =========\n");
        printf(" Usuario actual: %s\n", seg_usuario_actual());
        printf(" 1. Nueva partida\n");
        printf(" 2. Continuar partida (datos/partida_%s.sav)\n", seg_usuario_actual());
        printf(" 3. Ver tabla de récords (guardados en minios.disk)\n");
        printf(" 0. Volver al menú principal\n");

        int op = leer_entero(" Opción: ");
        if (op == 0 || feof(stdin)) break;

        if (op == 1) {
            if (!seg_verificar(OBJ_JUEGOS, D_EXEC)) continue;
            if (!seg_verificar(OBJ_PARTIDAS, D_CREATE)) continue;
            Partida p;
            inicializar_partida(&p, seg_usuario_actual());
            ejecutar_bucle_snake(&p);
        } else if (op == 2) {
            if (!seg_verificar(OBJ_JUEGOS, D_EXEC)) continue;
            if (!seg_verificar(OBJ_PARTIDAS, D_READ)) continue;
            Partida p;
            if (cargar_partida(seg_usuario_actual(), &p) != 0) {
                printf("  [AVISO] El usuario '%s' no tiene ninguna partida guardada en disco.\n",
                       seg_usuario_actual());
            } else {
                char fecha[32];
                time_t t = (time_t)p.guardada_en;
                struct tm *tm_info = localtime(&t);
                strftime(fecha, sizeof fecha, "%Y-%m-%d %H:%M:%S", tm_info);
                printf("  [PARTIDA CARGADA] Jugador: %s | Puntaje: %d | Nivel: %d | Largo: %d | Guardada: %s\n",
                       p.jugador, p.puntaje, p.nivel, p.largo, fecha);
                ejecutar_bucle_snake(&p);
            }
        } else if (op == 3) {
            if (seg_verificar(OBJ_REGISTROS, D_READ)) {
                fs_listar_records();
            }
        } else {
            printf("Opción inválida.\n");
        }
    }
}
