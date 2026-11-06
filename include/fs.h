#ifndef FS_H
#define FS_H

#include <stddef.h>

/* Disco virtual y CRUD de registros (Mejora 2) */
int  fs_montar(const char *ruta);
void fs_desmontar(void);
void fs_crear(void);
void fs_listar(void);
void fs_actualizar(void);
void fs_borrar(void);
void fs_estado(void);

/* Integración de récords del videojuego en el disco virtual (Mejora 4) */
int  fs_registrar_record(const char *jugador, int puntaje, int nivel);
void fs_listar_records(void);

/* Primitivas de persistencia binaria con open/read/write/fsync/close (Capa 1) */
int  fs_guardar_bloque(const char *ruta, const void *buf, size_t tam);
int  fs_cargar_bloque(const char *ruta, void *buf, size_t tam);

/* Funciones conservadas de MiniOS v2.0 (Gestor de archivos y Bitácora con syscalls) */
void fs_log_evento(const char *nivel, const char *mensaje);
void fs_mostrar_bitacora(void);
void fs_menu_gestor_v2(void);

#endif
