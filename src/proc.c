#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <sys/wait.h>
#include "kernel.h"
#include "proc.h"
#include "seguridad.h"
#include "fs.h"

static PCB tabla_procesos[MAX_PROCESOS];
static int num_procesos = 0;

static const char *texto_estado(Estado e) {
    switch (e) {
        case EJECUTANDO: return "EJECUTANDO";
        case SUSPENDIDO: return "SUSPENDIDO";
        case TERMINADO:  return "TERMINADO";
        default:         return "DESCONOCIDO";
    }
}

static void actualizar_zombis(void) {
    for (int i = 0; i < num_procesos; i++) {
        if (tabla_procesos[i].estado != TERMINADO && tabla_procesos[i].pid > 0) {
            int st = 0;
            pid_t r = waitpid(tabla_procesos[i].pid, &st, WNOHANG | WUNTRACED);
            if (r == tabla_procesos[i].pid) {
                if (WIFSTOPPED(st)) {
                    tabla_procesos[i].estado = SUSPENDIDO;
                } else if (WIFEXITED(st) || WIFSIGNALED(st)) {
                    tabla_procesos[i].estado = TERMINADO;
                }
            } else if (r == -1) {
                tabla_procesos[i].estado = TERMINADO;
            }
        }
    }
}

static void esperar_hijo(PCB *p) {
    int st = 0;
    if (waitpid(p->pid, &st, WUNTRACED) > 0) { /* vuelve si termina O si se detiene */
        if (WIFSTOPPED(st)) {
            p->estado = SUSPENDIDO;
            printf("\n  [PROCESOS] Proceso '%s' (PID %d) SUSPENDIDO (Ctrl+Z).\n", p->nombre, (int)p->pid);
        } else {
            p->estado = TERMINADO;
            printf("\n  [PROCESOS] Proceso '%s' (PID %d) TERMINADO.\n", p->nombre, (int)p->pid);
        }
    } else {
        p->estado = TERMINADO;
    }
}

void proc_lanzar(PCB *p, const char *programa) {
    struct termios term_orig;
    int tiene_tty = (tcgetattr(STDIN_FILENO, &term_orig) == 0);

    signal(SIGTSTP, SIG_IGN); /* Ctrl+Z no detiene a MiniOS */
    signal(SIGINT,  SIG_IGN); /* Ctrl+C no mata a MiniOS */
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        signal(SIGINT, SIG_DFL);
        return;
    }

    if (pid == 0) { /* proceso HIJO */
        setpgid(0, 0);
        signal(SIGTSTP, SIG_DFL); /* el juego sí responde a Ctrl+Z y Ctrl+C */
        signal(SIGINT,  SIG_DFL);
        signal(SIGTTOU, SIG_DFL);
        signal(SIGTTIN, SIG_DFL);

        execlp(programa, programa, (char *)NULL);

        /* Intentar en /usr/games/<programa> si /usr/games no está en PATH */
        char ruta_games[128];
        snprintf(ruta_games, sizeof ruta_games, "/usr/games/%s", programa);
        execl(ruta_games, programa, (char *)NULL);

        /* Si bsdgames no está instalado en la máquina anfitriona, ejecutar simulación
         * interactiva en el proceso hijo para que el control de procesos (Ctrl+Z,
         * SIGCONT, SIGTERM y Ctrl+C) funcione en cualquier Linux sin dependencias extra. */
        if (strcmp(programa, "tetris-bsd") == 0 ||
            strcmp(programa, "snake") == 0 ||
            strcmp(programa, "worm") == 0) {
            printf("\n[Modo portable: '%s' no instalado en /usr/games, ejecutando proceso hijo interactivo PID=%d]\n",
                   programa, (int)getpid());
            printf("Presione Ctrl+Z para suspender, Ctrl+C para terminar o 'q' + ENTER para salir.\n");
            fflush(stdout);
            char c;
            while (read(STDIN_FILENO, &c, 1) > 0) {
                if (c == 'q' || c == 'Q') _exit(0);
            }
            _exit(0);
        }

        perror("exec"); /* solo llega aquí si exec falla */
        _exit(127);
    }

    /* proceso PADRE (MiniOS) */
    setpgid(pid, pid);
    if (tiene_tty) {
        tcsetpgrp(STDIN_FILENO, pid);
    }

    memset(p, 0, sizeof *p);
    p->pid = pid;
    strncpy(p->nombre, programa, sizeof p->nombre - 1);
    strncpy(p->usuario, seg_usuario_actual(), sizeof p->usuario - 1);
    p->estado = EJECUTANDO;
    p->inicio = time(NULL);

    char msg[128];
    snprintf(msg, sizeof msg, "Proceso lanzado: %s (PID %d)", p->nombre, (int)p->pid);
    fs_log_evento("INFO", msg);

    esperar_hijo(p);

    if (tiene_tty) {
        tcsetpgrp(STDIN_FILENO, getpgrp());
        tcsetattr(STDIN_FILENO, TCSANOW, &term_orig);
        printf("\033[?25h\033[0m\n"); /* asegurar cursor visible y atributos normales */
        fflush(stdout);
    }
    signal(SIGINT, SIG_DFL); /* de vuelta en el menú, Ctrl+C vuelve a cerrar MiniOS */
}

void proc_reanudar(PCB *p) {
    if (!p || p->estado != SUSPENDIDO) {
        printf("  [ERROR] El proceso no está en estado SUSPENDIDO.\n");
        return;
    }
    if (!seg_verificar_propiedad(p->usuario)) {
        return;
    }

    struct termios term_orig;
    int tiene_tty = (tcgetattr(STDIN_FILENO, &term_orig) == 0);

    signal(SIGTSTP, SIG_IGN);
    signal(SIGINT,  SIG_IGN); /* mientras el juego está al frente */
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);

    if (tiene_tty) {
        tcsetpgrp(STDIN_FILENO, p->pid);
    }

    if (kill(p->pid, SIGCONT) == -1) {
        perror("kill(SIGCONT)");
        if (tiene_tty) tcsetpgrp(STDIN_FILENO, getpgrp());
        signal(SIGINT, SIG_DFL);
        return;
    }

    p->estado = EJECUTANDO;
    esperar_hijo(p);

    if (tiene_tty) {
        tcsetpgrp(STDIN_FILENO, getpgrp());
        tcsetattr(STDIN_FILENO, TCSANOW, &term_orig);
        printf("\033[?25h\033[0m\n");
        fflush(stdout);
    }
    signal(SIGINT, SIG_DFL);
}

void proc_terminar(PCB *p) {
    if (!p || p->estado == TERMINADO) {
        printf("  [AVISO] El proceso ya está terminado.\n");
        return;
    }
    if (!seg_verificar_propiedad(p->usuario)) {
        return;
    }

    /* Para terminar un proceso suspendido: SIGTERM seguido de SIGCONT y luego waitpid */
    kill(p->pid, SIGTERM);
    kill(p->pid, SIGCONT);

    int st = 0;
    waitpid(p->pid, &st, 0);
    p->estado = TERMINADO;

    printf("  [ÉXITO] Proceso '%s' (PID %d) terminado y recolectado sin dejar zombi.\n",
           p->nombre, (int)p->pid);
    fs_log_evento("INFO", "Proceso terminado con SIGTERM");
}

void proc_listar(void) {
    actualizar_zombis();
    printf("\n======================= TABLA DE PROCESOS (PCB) =======================\n");
    printf("%-8s %-16s %-12s %-14s %-20s\n", "PID", "PROGRAMA", "DUEÑO", "ESTADO", "HORA DE INICIO");
    printf("-----------------------------------------------------------------------\n");

    if (num_procesos == 0) {
        printf("  (No se han registrado procesos en esta sesión)\n");
    } else {
        for (int i = 0; i < num_procesos; i++) {
            char hora[32];
            struct tm *tm_info = localtime(&tabla_procesos[i].inicio);
            strftime(hora, sizeof hora, "%H:%M:%S", tm_info);
            printf("%-8d %-16s %-12s %-14s %-20s\n",
                   (int)tabla_procesos[i].pid,
                   tabla_procesos[i].nombre,
                   tabla_procesos[i].usuario,
                   texto_estado(tabla_procesos[i].estado),
                   hora);
        }
    }
    printf("=======================================================================\n");
}

static PCB *buscar_pcb_por_pid(pid_t pid) {
    actualizar_zombis();
    for (int i = 0; i < num_procesos; i++) {
        if (tabla_procesos[i].pid == pid) {
            return &tabla_procesos[i];
        }
    }
    return NULL;
}

static PCB *reservar_slot_pcb(void) {
    actualizar_zombis();
    if (num_procesos < MAX_PROCESOS) {
        return &tabla_procesos[num_procesos++];
    }
    /* Reutilizar una entrada TERMINADA si la tabla se llena */
    for (int i = 0; i < MAX_PROCESOS; i++) {
        if (tabla_procesos[i].estado == TERMINADO) {
            return &tabla_procesos[i];
        }
    }
    return NULL;
}

void proc_limpiar_todos(void) {
    for (int i = 0; i < num_procesos; i++) {
        if (tabla_procesos[i].estado != TERMINADO && tabla_procesos[i].pid > 0) {
            kill(tabla_procesos[i].pid, SIGTERM);
            kill(tabla_procesos[i].pid, SIGCONT);
            int st = 0;
            waitpid(tabla_procesos[i].pid, &st, 0);
            tabla_procesos[i].estado = TERMINADO;
        }
    }
}

void proc_menu(void) {
    while (1) {
        printf("\n========= GESTIÓN DE PROCESOS (BSDGames) =========\n");
        printf(" 1. Lanzar juego (tetris-bsd / snake / worm)\n");
        printf(" 2. Listar procesos (Tabla PCB)\n");
        printf(" 3. Reanudar un proceso suspendido (SIGCONT)\n");
        printf(" 4. Terminar un proceso (SIGTERM)\n");
        printf(" 0. Volver al menú principal\n");

        int op = leer_entero(" Opción: ");
        if (op == 0 || feof(stdin)) break;

        switch (op) {
            case 1: {
                if (!seg_verificar(OBJ_JUEGOS, D_EXEC)) break;
                printf("\nSeleccione el juego de BSDGames a lanzar:\n");
                printf("  1. tetris-bsd\n");
                printf("  2. snake\n");
                printf("  3. worm\n");
                printf("  4. Otro ejecutable\n");
                int j = leer_entero("  Juego: ");
                const char *prog = NULL;
                char custom[32];
                if (j == 1) prog = "tetris-bsd";
                else if (j == 2) prog = "snake";
                else if (j == 3) prog = "worm";
                else if (j == 4) {
                    leer_linea("  Nombre del programa: ", custom, sizeof custom);
                    if (custom[0] != '\0') prog = custom;
                }
                if (!prog) {
                    printf("  Selección cancelada.\n");
                    break;
                }
                PCB *slot = reservar_slot_pcb();
                if (!slot) {
                    printf("  [ERROR] Tabla de procesos llena.\n");
                    break;
                }
                printf("  Iniciando '%s'... (Use Ctrl+Z para suspender, Ctrl+C para terminar)\n", prog);
                proc_lanzar(slot, prog);
                break;
            }
            case 2:
                if (seg_verificar(OBJ_PROCESOS, D_READ)) {
                    proc_listar();
                }
                break;
            case 3: {
                if (!seg_verificar(OBJ_PROCESOS, D_UPDATE)) break;
                proc_listar();
                int pid = leer_entero("  PID del proceso a reanudar: ");
                if (pid <= 0) break;
                PCB *p = buscar_pcb_por_pid((pid_t)pid);
                if (!p) {
                    printf("  [ERROR] No se encontró ningún proceso con PID %d.\n", pid);
                } else {
                    proc_reanudar(p);
                }
                break;
            }
            case 4: {
                if (!seg_verificar(OBJ_PROCESOS, D_DELETE)) break;
                proc_listar();
                int pid = leer_entero("  PID del proceso a terminar: ");
                if (pid <= 0) break;
                PCB *p = buscar_pcb_por_pid((pid_t)pid);
                if (!p) {
                    printf("  [ERROR] No se encontró ningún proceso con PID %d.\n", pid);
                } else {
                    proc_terminar(p);
                }
                break;
            }
            default:
                printf("Opción inválida.\n");
                break;
        }
    }
}
