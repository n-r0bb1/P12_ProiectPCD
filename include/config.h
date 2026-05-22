#ifndef CONFIG_H
#define CONFIG_H

/* Loaded server configuration */
typedef struct {
    int max_workers;
    int chunk_size;
    int threshold;
} ServerConfig;

//Incarca config file
//Returneaza 0 pt succes, -1 pt eroare
int config_load(const char *path, ServerConfig *out);

#endif // CONFIG_H
