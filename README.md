# MiniOS v3.0 — Taller 1 (Parte 1)

Actualización de **MiniOS v2.0** a **MiniOS v3.0**, incorporando arquitectura modular por capas con llamadas al sistema POSIX directas, sistema de archivos sobre disco virtual (`minios.disk`) con superbloque y acceso directo, control de procesos (`PCB`, suspensión `Ctrl+Z`, reanudación `SIGCONT`, terminación `SIGTERM` sin procesos zombis), videojuego Snake en terminal con persistencia de sesión por usuario (`datos/partida_<usuario>.sav`), autoguardado con `Ctrl+C` y tabla de récords en el disco virtual, y subsistema de protección con usuarios, roles, propiedad y Matriz de Acceso persistente (`datos/matriz.dat`).

---

## 1. Integrantes y Compilación

### Integrantes
- Luis Alejandro Santander Balanta
- Kevin Cano Medina
- Daniel Angulo
- Ivan Caiza

### Requisitos y Cómo Compilar
El proyecto compila en Linux (Ubuntu / Kali / Debian) con `gcc` usando las banderas `-Wall -Wextra` sin ninguna advertencia.

```bash
# Limpiar binarios previos y compilar desde cero
make clean && make

# Ejecutar MiniOS v3.0
./minios
```

Para instalar los juegos clásicos de BSD en Kali/Ubuntu (si no están instalados):
```bash
sudo apt-get update && sudo apt-get install -y bsdgames strace
```

---

## 2. Arquitectura y Diagrama de Capas (Mejora 1)

El sistema está diseñado bajo una **arquitectura estricta por capas**, donde cada capa únicamente invoca servicios de las capas inferiores y nunca abre archivos ni crea procesos saltándose su nivel de abstracción:

```
+-----------------------------------------------------------------------+
| CAPA 4 — INTERFAZ DE USUARIO (CLI)                                    |
|   Archivo: src/main.c (y lectura segura en src/util.c)                |
|   Responsabilidad: Arranque, inicio de sesión y bucle infinito del    |
|   menú. Consulta primero a la Capa 2 (seg_verificar) antes de invocar |
|   cualquier operación. No llama a open() ni a fork().                 |
+-----------------------------------+-----------------------------------+
                                    | Llama a Capas 3, 2 y 1
                                    v
+-----------------------------------------------------------------------+
| CAPA 3 — SERVICIOS DEL SISTEMA                                        |
|   Archivos: src/proc.c | src/juego.c | src/sistema.c                  |
|   Responsabilidad:                                                    |
|   - proc.c: Tabla PCB, fork+exec de BSDGames, waitpid(WUNTRACED),     |
|     manejo de señales (SIGTSTP, SIGINT, SIGCONT, SIGTERM) sin zombis. |
|   - juego.c: Videojuego Snake en tiempo real (termios/select),        |
|     sesión por usuario (partida_<usuario>.sav) y récords en disco.    |
|   - sistema.c: Información del host y kernel vía uname(), sysconf(),  |
|     getpid() y getuid().                                              |
+-----------------------------------+-----------------------------------+
                                    | Llama a Capas 2 y 1
                                    v
+-----------------------------------------------------------------------+
| CAPA 2 — PROTECCIÓN Y SEGURIDAD                                       |
|   Archivo: src/seguridad.c                                            |
|   Responsabilidad: Dominios (admin, operador, invitado), 6 objetos    |
|   protegidos, matriz de acceso en memoria y persistente en disco      |
|   (datos/matriz.dat), mediación completa, fallo seguro y propiedad.   |
+-----------------------------------+-----------------------------------+
                                    | Llama a Capa 1
                                    v
+-----------------------------------------------------------------------+
| CAPA 1 — SISTEMA DE ARCHIVOS Y DISCO VIRTUAL                          |
|   Archivo: src/fs.c                                                   |
|   Responsabilidad: Montaje/desmontaje de datos/minios.disk,           |
|   superbloque (magic 0x4D494E49 "MINI", estado limpio/sucio),         |
|   acceso directo por ranura con lseek(), CRUD sin fragmentación,      |
|   persistencia de bloques binarios y gestor de archivos v2.0.         |
+-----------------------------------+-----------------------------------+
                                    | Llama a Capa 0
                                    v
+-----------------------------------------------------------------------+
| CAPA 0 — KERNEL LINUX (LLAMADAS AL SISTEMA POSIX)                     |
|   Syscalls: open, read, write, lseek, fsync, close, fork, execve,     |
|   waitpid, kill, rt_sigaction, ioctl (termios), select, uname, getpid |
+-----------------------------------------------------------------------+
```

### Estructura de Directorios
```
minios/
├── Makefile
├── README.md
├── include/
│   ├── kernel.h          ← versión, rutas y utilidades compartidas
│   ├── fs.h              ← disco virtual, CRUD y primitivas de archivo
│   ├── proc.h            ← tabla de procesos (PCB) y BSDGames
│   ├── juego.h           ← videojuego Snake y partida guardada
│   ├── seguridad.h       ← roles, objetos, derechos y matriz de acceso
│   └── sistema.h         ← "Acerca del sistema"
├── src/
│   ├── main.c            ← arranque, sesión y bucle del menú
│   ├── util.c            ← lectura segura de enteros y texto
│   ├── fs.c              ← implementación del disco virtual y CRUD
│   ├── proc.c            ← gestión de procesos y señales
│   ├── juego.c           ← motor de Snake y guardado de sesión
│   ├── seguridad.c       ← mecanismo y política de protección
│   └── sistema.c         ← consultas al kernel anfitrión
├── build/                ← generado por make (archivos .o)
└── datos/                ← generado por MiniOS: minios.disk, matriz.dat, partidas
```

---

## 3. Análisis de Llamadas al Sistema con `strace -c ./minios`

Al ejecutar `strace -c ./minios` realizando el montaje del disco virtual, operaciones de **Create** y **Read** de registros, consulta de **Acerca del sistema** y apagado limpio, se obtiene la siguiente tabla:

```text
% time     seconds  usecs/call     calls    errors syscall
------ ----------- ----------- --------- --------- ----------------
 40.25    0.013737          43       319           read
 22.65    0.007730          54       143           write
 19.27    0.006577          15       426           lseek
  8.19    0.002794          22       122           fsync
  4.47    0.001526          84        18         1 openat
  2.24    0.000766          40        19           close
  0.95    0.000324          32        10           newfstatat
  0.62    0.000210          17        12        11 mkdir
  0.59    0.000200          22         9           fstat
  0.09    0.000030          30         1           uname
  0.07    0.000025          25         1           getuid
  0.07    0.000024          24         1           getppid
  0.07    0.000023          23         1           getpid
------ ----------- ----------- --------- --------- ----------------
100.00    0.034130          30      1111        15 total
```

### Explicación de las llamadas observadas
1. **`openat` y `close`**: Abren los descriptores de archivo del disco virtual (`datos/minios.disk`), la matriz de acceso (`datos/matriz.dat`) y la bitácora (`datos/bitacora.log`) directamente en el kernel sin usar `FILE*` (`fopen`).
2. **`lseek`**: Posiciona el puntero del archivo exactamente en el desplazamiento `sizeof(Superbloque) + i * sizeof(Registro)` para realizar **acceso directo** a la ranura `i`, y en el byte `0` para actualizar el `Superbloque`.
3. **`read` y `write`**: Transfieren directamente estructuras binarias (`Superbloque`, `Registro`, `matriz`, `Partida`) entre el espacio de usuario de MiniOS y el descriptor de archivo del kernel.
4. **`fsync`**: Fuerza el vaciado de los búferes del caché de páginas del kernel hacia el almacenamiento secundario tras modificar registros, el superbloque o una partida guardada, garantizando durabilidad e integridad ante fallos.
5. **`uname`, `getpid`, `getppid`, `getuid`**: Invocadas por el módulo `src/sistema.c` en la opción *Acerca del sistema* para consultar la versión del kernel Linux anfitrión, el identificador del proceso actual y el usuario del sistema.

---

## 4. Comparación con la Historia de los Sistemas Operativos

**MiniOS v3.0** se asemeja a la transición histórica entre los **sistemas monolíticos estructurados por capas** (como el sistema **THE** de Edsger Dijkstra de 1968) y los **sistemas de tiempo compartido e interactivos tempranos tipo UNIX (3.ª y 4.ª generación)**. Mientras que en su versión v2.0 funcionaba como un simple monitor residente monousuario similar a los primeros sistemas de computador personal (como **CP/M** o **MS-DOS**, donde cualquier usuario tenía acceso total al disco y un programa en ejecución tomaba toda la máquina hasta terminar), en la v3.0 incorpora tres pilares propios de los sistemas multiusuario inspirados en **MULTICS** y **UNIX**:
1. **Estructuración jerárquica por capas**, aislando las llamadas al sistema del hardware/kernel anfitrión (Capa 0) respecto del sistema de archivos con superbloque (Capa 1), el mecanismo de protección (Capa 2), los servicios de procesos (Capa 3) y el intérprete de comandos (Capa 4).
2. **Gestión de procesos con control de trabajos (Job Control)** introducido en **BSD UNIX**, permitiendo crear procesos hijos (`fork` + `exec`), suspender interactivamente un proceso en primer plano con `Ctrl+Z` (`SIGTSTP`), mantener su estado en una tabla de bloques de control de procesos (`PCB`), reanudarlo con `SIGCONT` o finalizarlo con `SIGTERM` recolectando su estado con `waitpid` para evitar procesos zombis.
3. **Protección basada en dominios y Matriz de Acceso** (modelo formalizado por Butler Lampson en 1971), separando el mecanismo de verificación (`seg_verificar`) de la política almacenada en `matriz.dat`.

---

## 5. Matriz de Acceso y Justificación por Principios de Protección (Mejora 5)

### Tabla de la Matriz de Acceso por Defecto
Los derechos se codifican en bits: `C` = Create (`0x01`), `R` = Read (`0x02`), `U` = Update (`0x04`), `D` = Delete (`0x08`), `X` = Exec (`0x10`).

| Dominio (Rol) \ Objeto | `registros` | `juegos` | `partidas` | `procesos` | `matriz` | `sistema` (apagar) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **`admin`** | `CRUD-` | `----X` | `CRUD-` | `CRUD-` | `-RU--` | `----X` |
| **`operador`** | `CRU--` | `----X` | `CRU--` | `-RUD-` | `-R---` | `-----` |
| **`invitado`** | `-R---` | `----X` | `-----` | `-R---` | `-----` | `-----` |

### Justificación Celda por Celda con Principios de Protección

1. **`registros` (`minios.disk`)**:
   - **`admin` (`CRUD-`)**: **Separación de privilegios**. El administrador requiere control completo del disco virtual para tareas de mantenimiento y depuración de cualquier ranura.
   - **`operador` (`CRU--` + Propiedad)**: **Mínimo privilegio y Propiedad**. El operador puede crear registros (`C`), listarlos (`R`) y actualizar (`U`) únicamente aquellos registros donde `r.propietario` coincide con su usuario; no tiene permiso `D` por defecto para evitar borrados accidentales de información operativa.
   - **`invitado` (`-R---`)**: **Mínimo privilegio**. Un usuario no autenticado o temporal solo necesita consultar información (`R`) sin alterar el estado del disco virtual.

2. **`juegos` (BSDGames y motor Snake)**:
   - **`admin`, `operador`, `invitado` (`----X`)**: **Mínimo privilegio**. Sobre un programa ejecutable el único derecho aplicable es ejecutarlo (`X`); ningún rol requiere modificar los binarios de los juegos desde el menú.

3. **`partidas` (`datos/partida_<usuario>.sav`)**:
   - **`admin` (`CRUD-`)**: **Separación de privilegios**. Puede crear, cargar y gestionar sesiones de juego en disco.
   - **`operador` (`CRU--` + Propiedad)**: **Mínimo privilegio y Aislamiento por propietario**. Puede iniciar (`C`), cargar (`R`) y guardar/sobrescribir (`U`) exclusivamente su propio archivo `partida_operador.sav`.
   - **`invitado` (`-----`)**: **Fallo seguro y Mínimo privilegio**. Al ser una sesión efímera sin cuenta propia persistente, se le niega escribir archivos de sesión en disco para prevenir agotamiento de almacenamiento.

4. **`procesos` (Tabla PCB y señales)**:
   - **`admin` (`CRUD-`)**: **Separación de privilegios**. Puede listar todos los procesos activos/suspendidos (`R`), reanudar (`U`) y terminar (`D`) cualquier proceso del sistema para mantener la disponibilidad.
   - **`operador` (`-RUD-` + Propiedad)**: **Mínimo privilegio y Propiedad**. Puede inspeccionar la tabla PCB (`R`), pero el mecanismo verifica que solo pueda reanudar (`U` -> `SIGCONT`) o terminar (`D` -> `SIGTERM`) procesos cuyo campo `pcb.usuario` le pertenezca.
   - **`invitado` (`-R---`)**: **Mínimo privilegio**. Solo puede visualizar la tabla de procesos (`R`), sin enviar señales que detengan o alteren procesos en ejecución.

5. **`matriz` (`datos/matriz.dat`)**:
   - **`admin` (`-RU--` con autorretención)**: **Separación de privilegios y Fallo seguro**. Solo el administrador puede leer (`R`) y modificar (`U`) la política de seguridad. Además, el mecanismo impide que el `admin` se revoque a sí mismo los bits `R` o `U` sobre `matriz`, evitando dejar el sistema sin administrador de seguridad.
   - **`operador` (`-R---`)**: **Mínimo privilegio**. Puede consultar (`R`) qué permisos existen en el sistema para conocer sus capacidades, pero no alterar la política (`U`).
   - **`invitado` (`-----`)**: **Mínimo privilegio y Fallo seguro**. No requiere conocer ni modificar la estructura interna de permisos del sistema.

6. **`sistema` (Apagado y desmontaje de MiniOS)**:
   - **`admin` (`----X`)**: **Separación de privilegios**. Apagar el sistema operativo y desmontar el disco virtual es una operación crítica reservada exclusivamente al administrador.
   - **`operador` (`-----`) e `invitado` (`-----`)**: **Fallo seguro y Separación de privilegios**. Ningún usuario estándar o invitado puede apagar el sistema ni interrumpir la disponibilidad para otros procesos o sesiones.
