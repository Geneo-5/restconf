#include <fcgiapp.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http.h"
#include "restconf_handler.h"
#include "sysrepo_backend.h"
#include "ev_loop.h"

static int g_listen_sock = -1;
static int g_nthreads = 4;

static void *worker_main(void *arg)
{
    (void)arg;
    FCGX_Request request;
    FCGX_InitRequest(&request, g_listen_sock, 0);

    for (;;) {
        int rc = FCGX_Accept_r(&request);
        if (rc < 0) {
            break; /* socket ferme -> arret propre du thread */
        }

        struct http_request req;
        if (http_request_from_fcgx(&request, &req) == 0) {
            struct http_response resp;
            http_response_init(&resp);

            restconf_handle_request(&request, &req, &resp);

            /* RFC 8040 SS3.8/SS6 : un flux SSE (restconf_handle_request()
             * ayant deja ecrit directement sur 'request' et positionne
             * resp.already_sent) n'a pas de reponse bufferisee a envoyer
             * ici. */
            if (!resp.already_sent) {
                int head = (req.method && strcmp(req.method, "HEAD") == 0);
                http_response_flush(&request, &resp, head);
            }

            http_response_free(&resp);
            http_request_free(&req);
        }

        FCGX_Finish_r(&request);
    }
    return NULL;
}

int main(int argc, char **argv)
{
    /* Usage : restconfd [--sse-libev|--sse-threaded] [chemin-socket-unix|:port] [nb-threads] [racine-restconf]
     *
     * Sans argument, le processus suppose qu'il est lance par le
     * serveur web via le socket FastCGI standard (fd 0), ce qui
     * correspond au mode "spawn-fcgi"/systemd habituel avec nginx.
     *
     * --sse-libev / --sse-threaded (cf. README.md "Migration vers
     * libev") peuvent apparaitre n'importe ou dans la ligne de
     * commande ; ils sont retires avant l'analyse positionnelle
     * habituelle ci-dessous. --sse-libev n'a d'effet que si ce binaire
     * a ete construit avec -DBUILD_LIBEV=ON (sinon un avertissement est
     * affiche et le modele thread-per-flux reste utilise). */
    char *positional[3];
    int npositional = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--sse-libev") == 0) {
            g_restconf_sse_use_libev = 1;
        } else if (strcmp(argv[i], "--sse-threaded") == 0) {
            g_restconf_sse_use_libev = 0;
        } else if ((size_t)npositional < sizeof(positional) / sizeof(positional[0])) {
            positional[npositional++] = argv[i];
        }
    }

    const char *bind_spec = (npositional > 0) ? positional[0] : NULL;
    if (npositional > 1) {
        g_nthreads = atoi(positional[1]);
        if (g_nthreads < 1) {
            g_nthreads = 1;
        }
    }
    if (npositional > 2) {
        g_restconf_root = positional[2];
    }

    if (g_restconf_sse_use_libev && !restconf_ev_available()) {
        fprintf(stderr,
                "restconfd: --sse-libev demande mais ce binaire a ete construit sans le "
                "support libev (recompiler avec -DBUILD_LIBEV=ON) ; repli sur le modele "
                "thread-per-flux\n");
        g_restconf_sse_use_libev = 0;
    }

    signal(SIGPIPE, SIG_IGN);

    if (FCGX_Init() != 0) {
        fprintf(stderr, "FCGX_Init a echoue\n");
        return 1;
    }

    if (bind_spec) {
        g_listen_sock = FCGX_OpenSocket(bind_spec, 1024);
        if (g_listen_sock < 0) {
            fprintf(stderr, "FCGX_OpenSocket(%s) a echoue\n", bind_spec);
            return 1;
        }
    } else {
        g_listen_sock = 0; /* socket herite (FCGI_LISTENSOCK_FILENO) */
    }

    if (sysrepo_backend_init() != 0) {
        fprintf(stderr, "connexion a sysrepo impossible\n");
        return 1;
    }

    fprintf(stderr, "restconfd: racine RESTCONF=%s, %d thread(s), flux SSE via %s\n",
            g_restconf_root, g_nthreads, g_restconf_sse_use_libev ? "libev" : "thread-per-flux");

    if (g_restconf_sse_use_libev && restconf_ev_loop_start() != 0) {
        fprintf(stderr, "restconfd: demarrage de la boucle libev impossible\n");
        sysrepo_backend_destroy();
        return 1;
    }

    pthread_t *threads = calloc((size_t)g_nthreads, sizeof(pthread_t));
    for (int i = 0; i < g_nthreads; i++) {
        pthread_create(&threads[i], NULL, worker_main, NULL);
    }
    for (int i = 0; i < g_nthreads; i++) {
        pthread_join(threads[i], NULL);
    }
    free(threads);

    if (g_restconf_sse_use_libev) {
        restconf_ev_loop_stop();
    }

    sysrepo_backend_destroy();
    return 0;
}
