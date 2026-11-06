#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kernel.h"
#include "fs.h"
#include "seguridad.h"

#define ARCH_MATRIZ DIR_DATOS "matriz.dat"

/* POLÍTICA: filas = dominios (roles), columnas = objetos, celda = derechos */
static unsigned char matriz[NUM_ROLES][NUM_OBJETOS] = {
/*             registros                          juegos  partidas                            procesos                            matriz           sistema */
/* admin    */ { D_CREATE|D_READ|D_UPDATE|D_DELETE, D_EXEC, D_CREATE|D_READ|D_UPDATE|D_DELETE, D_CREATE|D_READ|D_UPDATE|D_DELETE, D_READ|D_UPDATE, D_EXEC },
/* operador */ { D_CREATE|D_READ|D_UPDATE,          D_EXEC, D_CREATE|D_READ|D_UPDATE,          D_READ|D_UPDATE|D_DELETE,          D_READ,          0      },
/* invitado */ { D_READ,                            D_EXEC, 0,                                 D_READ,                            0,               0      },
};

static const char *NOMBRE_ROL[NUM_ROLES] = { "admin", "operador", "invitado" };
static const char *NOMBRE_OBJ[NUM_OBJETOS] = {
    "registros", "juegos", "partidas", "procesos", "matriz", "sistema"
};

static int rol_actual = -1; /* -1 = sin sesión */

void seg_iniciar_sesion(void) { /* en el Taller 2 se cambia por login con clave */
    int r;
    do {
        r = leer_entero("Rol (0=admin 1=operador 2=invitado): ");
        if (feof(stdin)) {
            fs_desmontar();
            exit(0);
        }
        if (r < 0 || r >= NUM_ROLES) {
            printf("  Rol inválido. Ingrese 0, 1 o 2.\n");
        }
    } while (r < 0 || r >= NUM_ROLES);
    rol_actual = r;

    char msg[64];
    snprintf(msg, sizeof msg, "Inicio de sesión con rol: %s", NOMBRE_ROL[rol_actual]);
    fs_log_evento("INFO", msg);
}

const char *seg_usuario_actual(void) {
    return (rol_actual < 0 || rol_actual >= NUM_ROLES) ? "-" : NOMBRE_ROL[rol_actual];
}

Rol seg_rol_actual(void) {
    return (Rol)rol_actual;
}

/* MECANISMO: mediación completa + fallo seguro */
int seg_verificar(Objeto obj, unsigned char derecho) {
    if (rol_actual < 0 || rol_actual >= NUM_ROLES) {
        printf("  ACCESO DENEGADO (sin sesión activa)\n");
        return 0; /* fallo seguro */
    }
    if (obj < 0 || obj >= NUM_OBJETOS) {
        printf("  ACCESO DENEGADO (objeto inválido)\n");
        return 0;
    }
    if ((matriz[rol_actual][obj] & derecho) == derecho) {
        return 1;
    }
    printf("  ACCESO DENEGADO\n");
    fs_log_evento("WARNING", "Acceso denegado por matriz de protección");
    return 0;
}

/* Verificación de propiedad: el admin puede operar sobre todos; el operador solo sobre lo propio */
int seg_verificar_propiedad(const char *propietario) {
    if (rol_actual < 0 || rol_actual >= NUM_ROLES || !propietario) {
        printf("  ACCESO DENEGADO\n");
        return 0;
    }
    if (rol_actual == ROL_ADMIN) {
        return 1;
    }
    if (strcmp(seg_usuario_actual(), propietario) == 0) {
        return 1;
    }
    printf("  ACCESO DENEGADO: el recurso pertenece a '%s'\n", propietario);
    fs_log_evento("WARNING", "Acceso denegado por violación de propiedad");
    return 0;
}

void seg_guardar_matriz(void) {
    if (fs_guardar_bloque(ARCH_MATRIZ, matriz, sizeof matriz) != 0) {
        perror("matriz");
    }
}

void seg_cargar_matriz(void) {
    if (fs_cargar_bloque(ARCH_MATRIZ, matriz, sizeof matriz) != 0) {
        /* primera vez o archivo inválido: guarda la de fábrica */
        seg_guardar_matriz();
    }
    /* Garantizar regla de separación/no-bloqueo: admin siempre conserva lectura y edición de la matriz */
    matriz[ROL_ADMIN][OBJ_MATRIZ] |= (D_READ | D_UPDATE);
}

static void formatear_derechos(unsigned char d, char out[6]) {
    out[0] = (d & D_CREATE) ? 'C' : '-';
    out[1] = (d & D_READ)   ? 'R' : '-';
    out[2] = (d & D_UPDATE) ? 'U' : '-';
    out[3] = (d & D_DELETE) ? 'D' : '-';
    out[4] = (d & D_EXEC)   ? 'X' : '-';
    out[5] = '\0';
}

void seg_ver_matriz(void) {
    printf("\n==================== MATRIZ DE ACCESO ====================\n");
    printf("%-12s", "ROL \\ OBJ");
    for (int o = 0; o < NUM_OBJETOS; o++) {
        printf(" %-10s", NOMBRE_OBJ[o]);
    }
    printf("\n----------------------------------------------------------\n");

    for (int r = 0; r < NUM_ROLES; r++) {
        printf("%-12s", NOMBRE_ROL[r]);
        for (int o = 0; o < NUM_OBJETOS; o++) {
            char celda[6];
            formatear_derechos(matriz[r][o], celda);
            printf(" %-10s", celda);
        }
        printf("\n");
    }
    printf("----------------------------------------------------------\n");
    printf("Leyenda: C=Create(0x01) R=Read(0x02) U=Update(0x04) D=Delete(0x08) X=Exec(0x10)\n");
}

void seg_modificar_matriz(void) {
    if (rol_actual != ROL_ADMIN) {
        printf("  ACCESO DENEGADO: solo el administrador puede modificar la matriz.\n");
        return;
    }

    seg_ver_matriz();

    printf("\nSeleccione Dominio (Rol):\n");
    for (int r = 0; r < NUM_ROLES; r++) {
        printf("  %d = %s\n", r, NOMBRE_ROL[r]);
    }
    int r = leer_entero("Rol a modificar: ");
    if (r < 0 || r >= NUM_ROLES) {
        printf("Rol inválido.\n");
        return;
    }

    printf("\nSeleccione Objeto:\n");
    for (int o = 0; o < NUM_OBJETOS; o++) {
        printf("  %d = %s\n", o, NOMBRE_OBJ[o]);
    }
    int o = leer_entero("Objeto a modificar: ");
    if (o < 0 || o >= NUM_OBJETOS) {
        printf("Objeto inválido.\n");
        return;
    }

    char actual_str[6];
    formatear_derechos(matriz[r][o], actual_str);
    printf("\nDerechos actuales de [%s] sobre [%s]: %s (0x%02X)\n",
           NOMBRE_ROL[r], NOMBRE_OBJ[o], actual_str, matriz[r][o]);
    printf(" 1. Alternar CREATE (C)\n");
    printf(" 2. Alternar READ   (R)\n");
    printf(" 3. Alternar UPDATE (U)\n");
    printf(" 4. Alternar DELETE (D)\n");
    printf(" 5. Alternar EXEC   (X)\n");
    printf(" 6. Ingresar máscara en decimal/hex (0..31)\n");
    printf(" 0. Cancelar\n");

    int op = leer_entero("Opción: ");
    if (op <= 0 || op > 6) {
        printf("Operación cancelada.\n");
        return;
    }

    unsigned char nuevo = matriz[r][o];
    if (op == 1) nuevo ^= D_CREATE;
    else if (op == 2) nuevo ^= D_READ;
    else if (op == 3) nuevo ^= D_UPDATE;
    else if (op == 4) nuevo ^= D_DELETE;
    else if (op == 5) nuevo ^= D_EXEC;
    else if (op == 6) {
        int m = leer_entero("Nueva máscara (0..31): ");
        if (m < 0 || m > 31) {
            printf("Máscara inválida.\n");
            return;
        }
        nuevo = (unsigned char)m;
    }

    /* Regla de protección: el admin no puede quitarse a sí mismo el control de la matriz */
    if (r == ROL_ADMIN && o == OBJ_MATRIZ &&
        ((nuevo & (D_READ | D_UPDATE)) != (D_READ | D_UPDATE))) {
        printf("  [ERROR DE PROTECCIÓN] El admin no puede quitarse a sí mismo el control de la matriz.\n");
        return;
    }

    /* Regla adicional: el admin tampoco puede quitarse el permiso de apagar el sistema */
    if (r == ROL_ADMIN && o == OBJ_SISTEMA && !(nuevo & D_EXEC)) {
        printf("  [ERROR DE PROTECCIÓN] El admin debe conservar el derecho de apagar el sistema.\n");
        return;
    }

    matriz[r][o] = nuevo;
    seg_guardar_matriz();

    char nuevo_str[6];
    formatear_derechos(matriz[r][o], nuevo_str);
    printf("  [ÉXITO] Celda [%s][%s] actualizada a %s y guardada en disco.\n",
           NOMBRE_ROL[r], NOMBRE_OBJ[o], nuevo_str);
    fs_log_evento("INFO", "Matriz de acceso modificada por admin");
}
