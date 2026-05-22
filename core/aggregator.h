#ifndef AGGREGATOR_H
#define AGGREGATOR_H

#include <stddef.h>

//Functie ce aduna rezultatele worker process
long aggregator_sum(const long *partials, size_t count);

#endif // AGGREGATOR_H 
