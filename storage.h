#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <windows.h>

#define TAMANHO_PAGINA 4096
#define M 200 
#define MAX_PAGINAS_LIVRES 100 

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
    uint8_t preenchimento[TAMANHO_PAGINA - 24 - (MAX_PAGINAS_LIVRES * 8) - 4]; 
} SuperBloco;

// Funções exportadas por storage.c
FILE* inicializar_armazenamento(const char* nome_arquivo, bool *eh_novo);
bool ler_pagina(FILE* arquivo_db, int64_t id_pagina, PaginaBTree* pagina);
bool escrever_pagina(FILE* arquivo_db, int64_t id_pagina, const PaginaBTree* pagina);
bool ler_superbloco(FILE* arquivo_db, SuperBloco* sb);
bool escrever_superbloco(FILE* arquivo_db, const SuperBloco* sb);
int64_t alocar_pagina(FILE* arquivo_db, SuperBloco* sb);
void liberar_id_pagina(SuperBloco* sb, int64_t id_pagina);

// Adicione ao final do seu storage.h antes do #endif

#define MAX_FRAMES 16 // Quantidade de páginas simultâneas mantidas em RAM

typedef struct FrameBuffer {
    int64_t id_pagina;
    PaginaBTree pagina;
    int32_t pin_count;
    bool dirty; // Se true, precisa ser escrita em disco ao ser despejada
    struct FrameBuffer* prev;
    struct FrameBuffer* next;
} FrameBuffer;

typedef struct {
    FrameBuffer frames[MAX_FRAMES];
    FrameBuffer* lru_head; // Mais recentemente usado (MRU)
    FrameBuffer* lru_tail; // Menos recentemente usado (LRU) - Candidato a despejo
    CRITICAL_SECTION mutex_cache;
} BufferPool;

// Novas funções de controle do Buffer Cache
void inicializar_buffer_pool(BufferPool* pool);
void destruir_buffer_pool(BufferPool* pool, FILE* arquivo_db);
FrameBuffer* pin_pagina(BufferPool* pool, FILE* arquivo_db, int64_t id_pagina);
void unpin_pagina(BufferPool* pool, int64_t id_pagina, bool foi_modificada);
void flush_buffer_pool(BufferPool* pool, FILE* arquivo_db);

#endif // STORAGE_H