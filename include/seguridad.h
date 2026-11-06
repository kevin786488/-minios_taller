#ifndef SEGURIDAD_H
#define SEGURIDAD_H

typedef enum {
    ROL_ADMIN,
    ROL_OPERADOR,
    ROL_INVITADO,
    NUM_ROLES
} Rol; /* dominios */

typedef enum {
    OBJ_REGISTROS,
    OBJ_JUEGOS,
    OBJ_PARTIDAS,
    OBJ_PROCESOS,
    OBJ_MATRIZ,
    OBJ_SISTEMA,
    NUM_OBJETOS
} Objeto; /* objetos */

#define D_CREATE 0x01 /* derechos: un bit cada uno */
#define D_READ   0x02
#define D_UPDATE 0x04
#define D_DELETE 0x08
#define D_EXEC   0x10

void        seg_iniciar_sesion(void);
int         seg_verificar(Objeto obj, unsigned char derecho);
int         seg_verificar_propiedad(const char *propietario);
const char *seg_usuario_actual(void);
Rol         seg_rol_actual(void);
void        seg_cargar_matriz(void);
void        seg_guardar_matriz(void);
void        seg_ver_matriz(void);
void        seg_modificar_matriz(void);

#endif
