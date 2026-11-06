#include <stdio.h>
#include <unistd.h>
#include <pwd.h>
#include <sys/utsname.h>
#include "kernel.h"
#include "sistema.h"
#include "seguridad.h"
#include "fs.h"

void acerca_de(void) {
    struct utsname u;
    if (uname(&u) != 0) {
        perror("uname");
        return;
    }

    uid_t uid = getuid();
    struct passwd *pw = getpwuid(uid);
    const char *linux_user = (pw && pw->pw_name) ? pw->pw_name : "desconocido";
    long nucleos = sysconf(_SC_NPROCESSORS_ONLN);

    printf("\n================ ACERCA DEL SISTEMA ================\n");
    printf(" Sistema Operativo : MiniOS v%s\n", VERSION_MINIOS);
    printf(" Kernel Anfitrión  : %s %s (%s)\n", u.sysname, u.release, u.machine);
    printf(" Nodo / Hostname   : %s\n", u.nodename);
    printf(" Número de núcleos : %ld\n", nucleos);
    printf(" PID de MiniOS     : %d (PPID: %d)\n", (int)getpid(), (int)getppid());
    printf(" Usuario de Linux  : %s (UID: %d)\n", linux_user, (int)uid);
    printf(" Sesión MiniOS     : %s\n", seg_usuario_actual());
    printf("====================================================\n");

    fs_log_evento("INFO", "Consulta de Acerca del sistema");
}
