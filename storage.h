#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <windows.h> // Para CRITICAL_SECTION

#define TAMANHO_PAGINA 4096
#define M 200 
#define MAX_PAGINAS_LIVRES 100 
#define NUM_FRAMES 16

typedef int64_t deslocamento_disco_t;
#define DESLOCAMENTO_NULO -1

typedef struct {
    int32_t qtd_chaves;      
    int32_t eh_folha;        
    int64_t reservado;       
} CabecalhoPagina;

typedef struct {
    CabecalhoPagina cabecalho;
    int32_t chaves[M - 1];
    deslocamento_disco_t deslocamentos_registros[M - 1];
    deslocamento_disco_t deslocamentos_filhos[M];
    uint8_t preenchimento[TAMANHO_PAGINA - (sizeof(CabecalhoPagina) + 
                                           ((M - 1) * sizeof(int32_t)) + 
                                           ((M - 1) * sizeof(deslocamento_disco_t)) + 
                                           (M * sizeof(deslocamento_disco_t)))];
} PaginaBTree;

typedef struct {
    uint64_t numero_magico;                         
    int64_t proximo_id_pagina;                      
    int64_t id_pagina_raiz;                         
    int64_t paginas_livres[MAX_PAGINAS_LIVRES];     
    int32_t qtd_paginas_livres;                     
    int64_t proxima_pagina_livre;                   
    uint8_t preenchimento[TAMANHO_PAGINA - 24 - (MAX_PAGINAS_LIVRES * 8) - 12]; 
} SuperBloco;

typedef struct {
    int64_t id_pagina;
    PaginaBTree pagina;
    int32_t pin_count;
    bool dirty;
    CRITICAL_SECTION mutex_frame;
} FrameBuffer;

typedef struct {
    FrameBuffer pool[NUM_FRAMES];
    int32_t lru_lista[NUM_FRAMES];
    CRITICAL_SECTION mutex_pool;
} BufferPool;

extern uint64_t metrica_leituras_disco;
extern uint64_t metrica_escritas_disco;

void inicializar_buffer_pool(BufferPool* bp);
void destruir_buffer_pool(BufferPool* bp, FILE* arquivo);
FrameBuffer* pin_pagina(BufferPool* bp, FILE* arquivo, int64_t id_pagina);
void unpin_pagina(BufferPool* bp, int64_t id_pagina, bool dirty);
int64_t alocar_pagina(FILE* arquivo, SuperBloco* sb);
void liberar_id_pagina(SuperBloco* sb, int64_t id_pagina);
void ler_superbloco(FILE* arquivo, SuperBloco* sb);
void escrever_superbloco(FILE* arquivo, SuperBloco* sb);

#endif // STORAGE_H