#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>
#include <libgen.h>
#include <time.h>
#include "kernel.h"
#include "fs.h"
#include "seguridad.h"

#define MAGIC       0x4D494E49U /* "MINI" */
#define MAX_REG     100
#define BUFFER_SIZE 4096
#define ARCH_BITACORA DIR_DATOS "bitacora.log"

typedef struct {
    unsigned int magic, version, total, usados, siguiente_id, limpio;
} Superbloque;

typedef struct {
    int  id, activo; /* activo = 0 -> ranura libre */
    char nombre[50], descripcion[100], propietario[32];
} Registro;

static int disco_fd = -1;
static Superbloque sb;

static off_t pos_registro(int i) { /* acceso directo a la ranura i */
    return (off_t)sizeof(Superbloque) + (off_t)i * (off_t)sizeof(Registro);
}

static int leer_registro(int i, Registro *r) {
    if (disco_fd < 0 || i < 0 || i >= MAX_REG) return -1;
    if (lseek(disco_fd, pos_registro(i), SEEK_SET) == (off_t)-1) return -1;
    return read(disco_fd, r, sizeof *r) == (ssize_t)sizeof *r ? 0 : -1;
}

static int escribir_registro(int i, const Registro *r) {
    if (disco_fd < 0 || i < 0 || i >= MAX_REG) return -1;
    if (lseek(disco_fd, pos_registro(i), SEEK_SET) == (off_t)-1) return -1;
    if (write(disco_fd, r, sizeof *r) != (ssize_t)sizeof *r) return -1;
    fsync(disco_fd);
    return 0;
}

static void guardar_superbloque(void) {
    if (disco_fd < 0) return;
    lseek(disco_fd, 0, SEEK_SET);
    if (write(disco_fd, &sb, sizeof sb) == (ssize_t)sizeof sb) {
        fsync(disco_fd);
    }
}

int fs_guardar_bloque(const char *ruta, const void *buf, size_t tam) {
    mkdir("datos", 0755);
    int fd = open(ruta, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    ssize_t n = write(fd, buf, tam);
    fsync(fd);
    close(fd);
    return (n == (ssize_t)tam) ? 0 : -1;
}

int fs_cargar_bloque(const char *ruta, void *buf, size_t tam) {
    int fd = open(ruta, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = read(fd, buf, tam);
    close(fd);
    return (n == (ssize_t)tam) ? 0 : -1;
}

void fs_log_evento(const char *nivel, const char *mensaje) {
    mkdir("datos", 0755);
    int fd = open(ARCH_BITACORA, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return;

    time_t ahora = time(NULL);
    struct tm *tm_info = localtime(&ahora);
    char tiempo_str[32];
    strftime(tiempo_str, sizeof tiempo_str, "%Y-%m-%d %H:%M:%S", tm_info);

    char linea[512];
    int len = snprintf(linea, sizeof linea, "[%s] [%s] (%s) %s\n",
                       tiempo_str, nivel ? nivel : "INFO",
                       seg_usuario_actual(), mensaje ? mensaje : "");
    if (len > 0) {
        if ((size_t)len >= sizeof linea) len = (int)(sizeof linea - 1);
        ssize_t w = write(fd, linea, (size_t)len);
        (void)w;
        fsync(fd);
    }
    close(fd);
}

void fs_mostrar_bitacora(void) {
    int fd = open(ARCH_BITACORA, O_RDONLY);
    if (fd < 0) {
        printf("\nLa bitácora aún está vacía.\n");
        return;
    }
    printf("\n================ BITÁCORA DEL SISTEMA ================\n");
    char buf[1024];
    ssize_t n;
    while ((n = read(fd, buf, sizeof buf)) > 0) {
        ssize_t w = write(STDOUT_FILENO, buf, (size_t)n);
        (void)w;
    }
    printf("======================================================\n");
    close(fd);
}

int fs_montar(const char *ruta) {
    mkdir("datos", 0755);
    mkdir(DIR_DISK_V2, 0755);

    disco_fd = open(ruta, O_RDWR | O_CREAT, 0600);
    if (disco_fd < 0) {
        perror("open");
        return -1;
    }
    if (read(disco_fd, &sb, sizeof sb) != (ssize_t)sizeof sb || sb.magic != MAGIC) {
        printf("Disco sin formato: formateando...\n");
        memset(&sb, 0, sizeof sb);
        sb.magic = MAGIC;
        sb.version = 3;
        sb.total = MAX_REG;
        sb.usados = 0;
        sb.siguiente_id = 1;
        sb.limpio = 1;

        Registro vacio;
        memset(&vacio, 0, sizeof vacio);
        for (int i = 0; i < MAX_REG; i++) {
            escribir_registro(i, &vacio);
        }
    }
    if (!sb.limpio) {
        printf("ADVERTENCIA: el disco no se desmontó correctamente.\n");
    }
    sb.limpio = 0; /* en uso */
    guardar_superbloque();
    return 0;
}

void fs_desmontar(void) {
    if (disco_fd < 0) return;
    sb.limpio = 1;
    guardar_superbloque();
    close(disco_fd);
    disco_fd = -1;
}

void fs_crear(void) {
    int ranura = -1;
    Registro r;

    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 0) {
            ranura = i;
            break;
        }
    }

    if (ranura < 0) {
        printf("  [ERROR] Disco virtual lleno (%u/%u ranuras en uso).\n", sb.usados, sb.total);
        return;
    }

    char nombre[50];
    char descripcion[100];
    leer_linea("  Nombre del registro: ", nombre, sizeof nombre);
    if (nombre[0] == '\0') {
        printf("  [AVISO] Creación cancelada (nombre vacío).\n");
        return;
    }
    leer_linea("  Descripción: ", descripcion, sizeof descripcion);

    memset(&r, 0, sizeof r);
    r.id = (int)sb.siguiente_id++;
    r.activo = 1;
    strncpy(r.nombre, nombre, sizeof r.nombre - 1);
    strncpy(r.descripcion, descripcion, sizeof r.descripcion - 1);
    strncpy(r.propietario, seg_usuario_actual(), sizeof r.propietario - 1);

    if (escribir_registro(ranura, &r) == 0) {
        sb.usados++;
        guardar_superbloque();
        printf("  [ÉXITO] Registro creado -> ID: %d | Ranura: #%d | Propietario: %s\n",
               r.id, ranura, r.propietario);
        fs_log_evento("INFO", "Registro creado en disco virtual");
    } else {
        printf("  [ERROR] No se pudo escribir el registro en disco.\n");
    }
}

void fs_listar(void) {
    printf("\n=================== REGISTROS EN DISCO VIRTUAL ===================\n");
    printf("%-8s %-6s %-20s %-30s %-12s\n", "RANURA", "ID", "NOMBRE", "DESCRIPCIÓN", "PROPIETARIO");
    printf("------------------------------------------------------------------\n");

    int encontrados = 0;
    Registro r;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 1) {
            printf("#%-7d %-6d %-20s %-30s %-12s\n",
                   i, r.id, r.nombre, r.descripcion, r.propietario);
            encontrados++;
        }
    }

    if (encontrados == 0) {
        printf("  (No hay registros activos en el disco virtual)\n");
    }
    printf("------------------------------------------------------------------\n");
    printf("Total activos: %d / %u\n", encontrados, sb.total);
    fs_log_evento("INFO", "Lectura de registros del disco virtual");
}

void fs_actualizar(void) {
    int id = leer_entero("  ID del registro a actualizar: ");
    if (id <= 0) {
        printf("  ID inválido.\n");
        return;
    }

    Registro r;
    int ranura = -1;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 1 && r.id == id) {
            ranura = i;
            break;
        }
    }

    if (ranura < 0) {
        printf("  [ERROR] No existe un registro activo con ID %d.\n", id);
        return;
    }

    /* Verificación de propiedad (Mejora 5: el operador solo actualiza sus propios registros) */
    if (!seg_verificar_propiedad(r.propietario)) {
        return;
    }

    printf("  Registro actual en ranura #%d -> Nombre: '%s' | Descripción: '%s' | Dueño: %s\n",
           ranura, r.nombre, r.descripcion, r.propietario);

    char nuevo_nombre[50];
    char nueva_desc[100];
    leer_linea("  Nuevo nombre (ENTER para mantener actual): ", nuevo_nombre, sizeof nuevo_nombre);
    leer_linea("  Nueva descripción (ENTER para mantener actual): ", nueva_desc, sizeof nueva_desc);

    if (nuevo_nombre[0] != '\0') {
        strncpy(r.nombre, nuevo_nombre, sizeof r.nombre - 1);
        r.nombre[sizeof r.nombre - 1] = '\0';
    }
    if (nueva_desc[0] != '\0') {
        strncpy(r.descripcion, nueva_desc, sizeof r.descripcion - 1);
        r.descripcion[sizeof r.descripcion - 1] = '\0';
    }

    if (escribir_registro(ranura, &r) == 0) {
        guardar_superbloque();
        printf("  [ÉXITO] Registro ID %d actualizado en ranura #%d.\n", r.id, ranura);
        fs_log_evento("INFO", "Registro actualizado en disco virtual");
    } else {
        printf("  [ERROR] Fallo al actualizar registro.\n");
    }
}

void fs_borrar(void) {
    int id = leer_entero("  ID del registro a borrar: ");
    if (id <= 0) {
        printf("  ID inválido.\n");
        return;
    }

    Registro r;
    int ranura = -1;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 1 && r.id == id) {
            ranura = i;
            break;
        }
    }

    if (ranura < 0) {
        printf("  [ERROR] No existe un registro activo con ID %d.\n", id);
        return;
    }

    if (!seg_verificar_propiedad(r.propietario)) {
        return;
    }

    r.activo = 0; /* Se marca como libre sin corromper; el siguiente Create reutiliza esta ranura */
    if (escribir_registro(ranura, &r) == 0) {
        if (sb.usados > 0) sb.usados--;
        guardar_superbloque();
        printf("  [ÉXITO] Registro ID %d borrado. Ranura #%d marcada como libre.\n", id, ranura);
        fs_log_evento("INFO", "Registro borrado del disco virtual");
    } else {
        printf("  [ERROR] No se pudo borrar el registro.\n");
    }
}

void fs_estado(void) {
    printf("\n================ ESTADO DEL DISCO VIRTUAL ================\n");
    printf(" Magic Number   : 0x%08X (\"MINI\")\n", sb.magic);
    printf(" Versión FS     : %u\n", sb.version);
    printf(" Capacidad Total: %u ranuras (%zu bytes c/u)\n", sb.total, sizeof(Registro));
    printf(" Ranuras Usadas : %u\n", sb.usados);
    printf(" Ranuras Libres : %u\n", sb.total - sb.usados);
    printf(" Siguiente ID   : %u\n", sb.siguiente_id);
    printf(" Estado Superbl.: %s\n", sb.limpio ? "LIMPIO (1)" : "EN USO / SUCIO (0)");
    printf("----------------------------------------------------------\n");
    printf(" Mapa de Ocupación ([X]=Ocupada, [.]=Libre):\n ");

    Registro r;
    for (int i = 0; i < (int)sb.total; i++) {
        int ocupado = (leer_registro(i, &r) == 0 && r.activo == 1);
        printf("%s", ocupado ? "[X]" : "[.]");
        if ((i + 1) % 20 == 0 && (i + 1) < (int)sb.total) {
            printf("\n ");
        }
    }
    printf("\n==========================================================\n");
}

int fs_registrar_record(const char *jugador, int puntaje, int nivel) {
    int ranura = -1;
    Registro r;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 0) {
            ranura = i;
            break;
        }
    }
    if (ranura < 0) return -1;

    memset(&r, 0, sizeof r);
    r.id = (int)sb.siguiente_id++;
    r.activo = 1;
    strncpy(r.nombre, "RECORD_SNAKE", sizeof r.nombre - 1);
    snprintf(r.descripcion, sizeof r.descripcion, "Puntaje: %d | Nivel: %d", puntaje, nivel);
    strncpy(r.propietario, jugador ? jugador : seg_usuario_actual(), sizeof r.propietario - 1);

    if (escribir_registro(ranura, &r) == 0) {
        sb.usados++;
        guardar_superbloque();
        return r.id;
    }
    return -1;
}

void fs_listar_records(void) {
    printf("\n================ TABLA DE RÉCORDS (DISCO VIRTUAL) ================\n");
    printf("%-6s %-15s %-30s\n", "ID", "JUGADOR", "RESULTADO");
    printf("------------------------------------------------------------------\n");
    int count = 0;
    Registro r;
    for (int i = 0; i < (int)sb.total; i++) {
        if (leer_registro(i, &r) == 0 && r.activo == 1 && strcmp(r.nombre, "RECORD_SNAKE") == 0) {
            printf("%-6d %-15s %-30s\n", r.id, r.propietario, r.descripcion);
            count++;
        }
    }
    if (count == 0) {
        printf("  (Aún no hay récords registrados en el disco virtual)\n");
    }
    printf("==================================================================\n");
}

/* =========================================================================
 * Funciones heredadas de MiniOS v2.0 (Gestor de archivos en disk/ con POSIX)
 * ========================================================================= */

static bool es_ruta_segura_v2(const char *ruta_relativa, char *ruta_absoluta_out) {
    mkdir(DIR_DISK_V2, 0755);
    char base_abs[2048];
    if (realpath(DIR_DISK_V2, base_abs) == NULL) {
        return false;
    }

    char target_path[4096];
    if (ruta_relativa == NULL || strlen(ruta_relativa) == 0 || strcmp(ruta_relativa, ".") == 0) {
        snprintf(target_path, sizeof target_path, "%s", base_abs);
    } else if (ruta_relativa[0] == '/') {
        snprintf(target_path, sizeof target_path, "%s%s", base_abs, ruta_relativa);
    } else {
        snprintf(target_path, sizeof target_path, "%s/%s", base_abs, ruta_relativa);
    }

    char resolved_target[4096];
    if (realpath(target_path, resolved_target) != NULL) {
        size_t base_len = strlen(base_abs);
        if (strncmp(base_abs, resolved_target, base_len) == 0 &&
            (resolved_target[base_len] == '\0' || resolved_target[base_len] == '/')) {
            if (ruta_absoluta_out != NULL) {
                strcpy(ruta_absoluta_out, resolved_target);
            }
            return true;
        }
        return false;
    } else {
        char tmp_path[4096];
        snprintf(tmp_path, sizeof tmp_path, "%s", target_path);
        char *dir_name = dirname(tmp_path);
        char resolved_parent[2048];

        if (realpath(dir_name, resolved_parent) != NULL) {
            size_t base_len = strlen(base_abs);
            if (strncmp(base_abs, resolved_parent, base_len) == 0 &&
                (resolved_parent[base_len] == '\0' || resolved_parent[base_len] == '/')) {
                char tmp_base[4096];
                snprintf(tmp_base, sizeof tmp_base, "%s", target_path);
                char *base_name = basename(tmp_base);
                int written = snprintf(resolved_target, sizeof resolved_target, "%s/%s", resolved_parent, base_name);
                if (written > 0 && (size_t)written < sizeof resolved_target) {
                    if (ruta_absoluta_out != NULL) {
                        strcpy(ruta_absoluta_out, resolved_target);
                    }
                    return true;
                }
            }
        }
        return false;
    }
}

static void fm_listar_directorio(const char *subruta) {
    char ruta_abs[2048];
    if (!es_ruta_segura_v2(subruta, ruta_abs)) {
        printf("[Error de Seguridad] Ruta no permitida o no válida.\n");
        return;
    }
    DIR *dir = opendir(ruta_abs);
    if (!dir) {
        perror("opendir");
        return;
    }
    printf("\n--- CONTENIDO DEL DIRECTORIO: %s ---\n", (subruta && subruta[0]) ? subruta : "/");
    printf("%-25s %-10s %-12s\n", "Nombre", "Tipo", "Tamaño (Bytes)");
    printf("---------------------------------------------------\n");
    struct dirent *entrada;
    while ((entrada = readdir(dir)) != NULL) {
        if (strcmp(entrada->d_name, ".") == 0 || strcmp(entrada->d_name, "..") == 0) continue;
        char full_path[4096];
        snprintf(full_path, sizeof full_path, "%s/%s", ruta_abs, entrada->d_name);
        struct stat st;
        if (stat(full_path, &st) == 0) {
            const char *tipo = S_ISDIR(st.st_mode) ? "[DIR]" : "[FILE]";
            printf("%-25s %-10s %-12lld\n", entrada->d_name, tipo, (long long)st.st_size);
        }
    }
    closedir(dir);
}

static void fm_crear_archivo_texto(const char *subruta) {
    char ruta_abs[2048];
    if (!es_ruta_segura_v2(subruta, ruta_abs)) {
        printf("[Error de Seguridad] Ruta fuera del disco.\n");
        return;
    }
    int fd = open(ruta_abs, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("open");
        return;
    }
    char buffer[1024];
    leer_linea("Ingrese el texto para el archivo:\n> ", buffer, sizeof buffer);
    size_t len = strlen(buffer);
    if (len + 1 < sizeof buffer) {
        buffer[len++] = '\n';
    }
    if (write(fd, buffer, len) >= 0) {
        fsync(fd);
        printf("[Éxito] Archivo creado correctamente.\n");
    }
    close(fd);
}

static void fm_leer_archivo(const char *subruta) {
    char ruta_abs[2048];
    if (!es_ruta_segura_v2(subruta, ruta_abs)) {
        printf("[Error de Seguridad] Ruta no permitida o no existe.\n");
        return;
    }
    int fd = open(ruta_abs, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return;
    }
    char buffer[BUFFER_SIZE];
    ssize_t n;
    printf("\n--- CONTENIDO DE '%s' ---\n", subruta);
    while ((n = read(fd, buffer, sizeof buffer)) > 0) {
        ssize_t w = write(STDOUT_FILENO, buffer, (size_t)n);
        (void)w;
    }
    printf("\n-------------------------------\n");
    close(fd);
}

static void fm_editar_archivo(const char *subruta) {
    char ruta_abs[2048];
    if (!es_ruta_segura_v2(subruta, ruta_abs)) {
        printf("[Error de Seguridad] Ruta no permitida o no existe.\n");
        return;
    }
    int fd = open(ruta_abs, O_WRONLY | O_APPEND);
    if (fd < 0) {
        perror("open");
        return;
    }
    char buffer[1024];
    leer_linea("Ingrese el texto a añadir al final:\n> ", buffer, sizeof buffer);
    size_t len = strlen(buffer);
    if (len + 1 < sizeof buffer) {
        buffer[len++] = '\n';
    }
    if (write(fd, buffer, len) >= 0) {
        fsync(fd);
        printf("[Éxito] Archivo actualizado correctamente.\n");
    }
    close(fd);
}

static void fm_eliminar_v2(const char *subruta) {
    char ruta_abs[2048];
    if (!es_ruta_segura_v2(subruta, ruta_abs)) {
        printf("[Error de Seguridad] Ruta no válida.\n");
        return;
    }
    struct stat st;
    if (stat(ruta_abs, &st) == -1) {
        perror("stat");
        return;
    }
    int res = S_ISDIR(st.st_mode) ? rmdir(ruta_abs) : unlink(ruta_abs);
    if (res == 0) printf("[Éxito] Elemento eliminado.\n");
    else perror("eliminar");
}

void fs_menu_gestor_v2(void) {
    while (1) {
        printf("\n--- GESTOR DE DIRECTORIO Y BITÁCORA (v2.0 compatible) ---\n");
        printf(" 1. Listar directorio (disk/)\n");
        printf(" 2. Crear archivo de texto en disk/\n");
        printf(" 3. Leer archivo de disk/\n");
        printf(" 4. Añadir texto a archivo de disk/\n");
        printf(" 5. Eliminar archivo de disk/\n");
        printf(" 6. Ver bitácora del sistema (datos/bitacora.log)\n");
        printf(" 0. Volver al menú principal\n");

        int op = leer_entero(" Opción: ");
        if (op == 0 || feof(stdin)) break;

        char ruta[256];
        switch (op) {
            case 1:
                if (seg_verificar(OBJ_REGISTROS, D_READ)) {
                    leer_linea("Subruta (ENTER para raíz): ", ruta, sizeof ruta);
                    fm_listar_directorio(ruta);
                }
                break;
            case 2:
                if (seg_verificar(OBJ_REGISTROS, D_CREATE)) {
                    leer_linea("Nombre del archivo: ", ruta, sizeof ruta);
                    fm_crear_archivo_texto(ruta);
                }
                break;
            case 3:
                if (seg_verificar(OBJ_REGISTROS, D_READ)) {
                    leer_linea("Nombre del archivo: ", ruta, sizeof ruta);
                    fm_leer_archivo(ruta);
                }
                break;
            case 4:
                if (seg_verificar(OBJ_REGISTROS, D_UPDATE)) {
                    leer_linea("Nombre del archivo: ", ruta, sizeof ruta);
                    fm_editar_archivo(ruta);
                }
                break;
            case 5:
                if (seg_verificar(OBJ_REGISTROS, D_DELETE)) {
                    leer_linea("Nombre del archivo: ", ruta, sizeof ruta);
                    fm_eliminar_v2(ruta);
                }
                break;
            case 6:
                if (seg_verificar(OBJ_REGISTROS, D_READ)) {
                    fs_mostrar_bitacora();
                }
                break;
            default:
                printf("Opción inválida.\n");
                break;
        }
    }
}
