/* threeds.c – libconfig-based ServerConfig loader */
#include "../include/config.h"  //Header-ul care defineste structura ServerConfig si prototipul functiei config_load

#include <stdio.h>      //Pentru fprintf (afisare erori in stderr)
#include <libconfig.h>  //Biblioteca externa pentru citirea fisierelor de configurare tip .cfg

#define DEFAULT_MAX_WORKERS 4  //Valoare implicita pentru numarul maxim de worker threads/procese
#define DEFAULT_CHUNK_SIZE  100 //Valoare implicita pentru dimensiunea chunk-urilor de procesare
#define DEFAULT_THRESHOLD   25  //Valoare implicita pentru pragul (threshold) de procesare

// Încarcă configurația serverului din fișier 
int config_load(const char *path, ServerConfig *out)
{
    // Validare parametri de intrare 
    if (!path || !out) { //Verificam daca pointerii sunt validi (evitam dereferentiere NULL)
        return -1;       //Returnam eroare daca inputul nu este valid
    }

    // Inițializare structură cu zero (evită garbage values) 
    ServerConfig zero = {0, 0, 0}; //Structura temporara initializata cu 0 pentru toate campurile
    *out = zero;                   //Copiem structura zero in output pentru a evita valori nedefinite

    // Inițializare context libconfig
    config_t cfg;                  //Structura principala libconfig care tine starea parserului
    config_init(&cfg);            //Initializam contextul libconfig (obligatoriu inainte de utilizare)

    // Citire fișier de configurare 
    if (config_read_file(&cfg, path) == CONFIG_FALSE) { //Incercam sa citim fisierul de configurare
        /* Afișare eroare de parsing din fișier */
        (void)fprintf(stderr, "[config] %s:%d - %s\n",
                      config_error_file(&cfg),   //Fisierul unde a aparut eroarea
                      config_error_line(&cfg),   //Linia unde s-a produs eroarea
                      config_error_text(&cfg));   //Mesajul de eroare

        config_destroy(&cfg); //Eliberam resursele libconfig in caz de eroare
        return -1;            //Returnam eroare la citirea config-ului
    }

    int val = 0; //Variabila temporara folosita pentru citirea valorilor din config

    // --- max_workers --- 
    // Citește server.max_workers sau folosește default 
    if (config_lookup_int(&cfg, "server.max_workers", &val) == CONFIG_TRUE
            && val > 0) { //Verificam daca valoarea exista si este valida (>0)
        out->max_workers = val; //Setam valoarea citita din config
    } else {
        out->max_workers = DEFAULT_MAX_WORKERS; //Fallback la valoare implicita
    }

    // --- chunk_size --- 
    // Citește server.chunk_size sau fallback 
    if (config_lookup_int(&cfg, "server.chunk_size", &val) == CONFIG_TRUE
            && val > 0) { //Validam existenta si corectitudinea valorii
        out->chunk_size = val; //Setam valoarea din config
    } else {
        out->chunk_size = DEFAULT_CHUNK_SIZE; //Fallback default
    }

    // --- threshold --- 
    // Citește server.threshold sau fallback 
    if (config_lookup_int(&cfg, "server.threshold", &val) == CONFIG_TRUE
            && val >= 0) { //Threshold poate fi 0 sau mai mare
        out->threshold = val; //Aplicam valoarea din config
    } else {
        out->threshold = DEFAULT_THRESHOLD; //Fallback la valoare implicita
    }

    // Eliberare resurse libconfig 
    config_destroy(&cfg); //Eliberam memoria interna folosita de parserul libconfig

    return 0; //Returnam succes (config incarcat corect)
}