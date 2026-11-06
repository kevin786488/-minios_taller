#include <stdio.h>
#include "kernel.h"
#include "fs.h"
#include "proc.h"
#include "juego.h"
#include "seguridad.h"
#include "sistema.h"

static void mostrar_menu(void) {
    printf("\n===== MiniOS v%s | usuario: %s =====\n", VERSION_MINIOS, seg_usuario_actual());
    printf(" 1. Create   2. Read   3. Update   4. Delete\n");
    printf(" 5. Estado del disco virtual\n");
    printf(" 6. Gestión de procesos (BSDGames)\n");
    printf(" 7. Videojuego con sesión en disco (Snake)\n");
    printf(" 8. Ver matriz de acceso\n");
    printf(" 9. Modificar matriz de acceso\n");
    printf("10. Cambiar de usuario / rol\n");
    printf("11. Acerca del sistema\n");
    printf("12. Gestor de archivos y Bitácora (v2.0)\n");
    printf(" 0. Apagar\n");
}

int main(void) {
    if (fs_montar(DIR_DATOS "minios.disk") != 0) return 1;
    seg_cargar_matriz();
    seg_iniciar_sesion();

    while (1) {
        mostrar_menu();
        switch (leer_entero(" Opción: ")) {
            case 1:
                if (seg_verificar(OBJ_REGISTROS, D_CREATE)) fs_crear();
                break;
            case 2:
                if (seg_verificar(OBJ_REGISTROS, D_READ))   fs_listar();
                break;
            case 3:
                if (seg_verificar(OBJ_REGISTROS, D_UPDATE)) fs_actualizar();
                break;
            case 4:
                if (seg_verificar(OBJ_REGISTROS, D_DELETE)) fs_borrar();
                break;
            case 5:
                if (seg_verificar(OBJ_REGISTROS, D_READ))   fs_estado();
                break;
            case 6:
                if (seg_verificar(OBJ_PROCESOS, D_READ))    proc_menu();
                break;
            case 7:
                if (seg_verificar(OBJ_JUEGOS, D_EXEC))      juego_menu();
                break;
            case 8:
                if (seg_verificar(OBJ_MATRIZ, D_READ))      seg_ver_matriz();
                break;
            case 9:
                if (seg_verificar(OBJ_MATRIZ, D_UPDATE))    seg_modificar_matriz();
                break;
            case 10:
                seg_iniciar_sesion();
                break;
            case 11:
                acerca_de();
                break;
            case 12:
                if (seg_verificar(OBJ_REGISTROS, D_READ))   fs_menu_gestor_v2();
                break;
            case 0:
                if (seg_verificar(OBJ_SISTEMA, D_EXEC)) {
                    proc_limpiar_todos();
                    fs_desmontar();
                    printf("Apagando MiniOS...\n");
                    return 0;
                }
                break;
            default:
                if (feof(stdin)) {
                    proc_limpiar_todos();
                    fs_desmontar();
                    return 0; /* Ctrl+D */
                }
                printf("Opción inválida.\n");
                break;
        }
    }
}
