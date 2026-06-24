#include "storage.h"
#include <string.h>
#include <stdlib.h>

#define ASSINATURA_MAGICA 0x4946455342545245ULL // "IFESBTRE" em Hexadecimal

FILE* inicializar_armazenamento(const char* nome_arquivo, bool *eh_novo) {
    FILE* arquivo = fopen(nome_arquivo, "rb+");
    
    if (arquivo == NULL) {
        arquivo = fopen(nome_arquivo, "wb+");
        if (arquivo == NULL) {
            perror("Erro ao criar o arquivo de banco de dados");
            return NULL;
        }
        *eh_novo = true;

        SuperBloco sb;
        memset(&sb, 0, sizeof(SuperBloco));
        sb.numero_magico = ASSINATURA_MAGICA;
        sb.proximo_id_pagina = 1; 
        sb.id_pagina_raiz = DESLOCAMENTO_NULO; 
        sb.qtd_paginas_livres = 0;

        if (!escrever_superbloco(arquivo, &sb)) {
            fprintf(stderr, "Erro fatal: falha ao inicializar o SuperBloco.\n");
            fclose(arquivo);
            return NULL;
        }
        
        fflush(arquivo);
    } else {
        *eh_novo = false;
        SuperBloco sb;
        if (!ler_superbloco(arquivo, &sb) || sb.numero_magico != ASSINATURA_MAGICA) {
            fprintf(stderr, "Erro: Arquivo corrompido ou formato inválido.\n");
            fclose(arquivo);
            return NULL;
        }
    }
    
    return arquivo;
}

bool ler_pagina(FILE* arquivo_db, int64_t id_pagina, PaginaBTree* pagina) {
    if (id_pagina < 1 || arquivo_db == NULL || pagina == NULL) return false;

    if (fseek(arquivo_db, id_pagina * TAMANHO_PAGINA, SEEK_SET) != 0) {
        perror("Erro ao executar fseek na leitura da página");
        return false;
    }

    size_t bytes_lidos = fread(pagina, 1, TAMANHO_PAGINA, arquivo_db);
    if (bytes_lidos != TAMANHO_PAGINA) {
        if (ferror(arquivo_db)) {
            perror("Erro de leitura física na página");
        }
        return false;
    }

    return true;
}

bool escrever_pagina(FILE* arquivo_db, int64_t id_pagina, const PaginaBTree* pagina) {
    if (id_pagina < 1 || arquivo_db == NULL || pagina == NULL) return false;

    if (fseek(arquivo_db, id_pagina * TAMANHO_PAGINA, SEEK_SET) != 0) {
        perror("Erro ao executar fseek na escrita da página");
        return false;
    }

    size_t bytes_escritos = fwrite(pagina, 1, TAMANHO_PAGINA, arquivo_db);
    if (bytes_escritos != TAMANHO_PAGINA) {
        perror("Erro de escrita física na página");
        return false;
    }

    return true;
}

bool ler_superbloco(FILE* arquivo_db, SuperBloco* sb) {
    if (arquivo_db == NULL || sb == NULL) return false;

    if (fseek(arquivo_db, 0, SEEK_SET) != 0) {
        return false;
    }

    return fread(sb, 1, TAMANHO_PAGINA, arquivo_db) == TAMANHO_PAGINA;
}

bool escrever_superbloco(FILE* arquivo_db, const SuperBloco* sb) {
    if (arquivo_db == NULL || sb == NULL) return false;

    if (fseek(arquivo_db, 0, SEEK_SET) != 0) {
        return false;
    }

    return fwrite(sb, 1, TAMANHO_PAGINA, arquivo_db) == TAMANHO_PAGINA;
}

int64_t alocar_pagina(FILE* arquivo_db, SuperBloco* sb) {
    if (sb->qtd_paginas_livres > 0) {
        sb->qtd_paginas_livres--;
        int64_t id_reutilizado = sb->paginas_livres[sb->qtd_paginas_livres];
        
        escrever_superbloco(arquivo_db, sb);
        return id_reutilizado;
    }

    int64_t novo_id = sb->proximo_id_pagina;
    sb->proximo_id_pagina++;
    
    escrever_superbloco(arquivo_db, sb);
    return novo_id;
}

void liberar_id_pagina(SuperBloco* sb, int64_t id_pagina) {
    if (sb->qtd_paginas_livres < MAX_PAGINAS_LIVRES) {
        sb->paginas_livres[sb->qtd_paginas_livres] = id_pagina;
        sb->qtd_paginas_livres++;
    }
}

void inicializar_buffer_pool(BufferPool* pool) {
    InitializeCriticalSection(&pool->mutex_cache);
    pool->lru_head = NULL;
    pool->lru_tail = NULL;
    
    for (int i = 0; i < MAX_FRAMES; i++) {
        pool->frames[i].id_pagina = DESLOCAMENTO_NULO;
        pool->frames[i].pin_count = 0;
        pool->frames[i].dirty = false;
        pool->frames[i].prev = NULL;
        pool->frames[i].next = NULL;
    }
}

static void mover_para_cabeca_lru(BufferPool* pool, FrameBuffer* frame) {
    if (pool->lru_head == frame) return;

    // Desconecta o frame de sua posição atual
    if (frame->prev) frame->prev->next = frame->next;
    if (frame->next) frame->next->prev = frame->prev;

    if (pool->lru_tail == frame) {
        pool->lru_tail = frame->prev;
    }

    // Insere na cabeça (Mais Recentemente Usado)
    frame->next = pool->lru_head;
    frame->prev = NULL;
    if (pool->lru_head) {
        pool->lru_head->prev = frame;
    }
    pool->lru_head = frame;

    if (!pool->lru_tail) {
        pool->lru_tail = frame;
    }
}

FrameBuffer* pin_pagina(BufferPool* pool, FILE* arquivo_db, int64_t id_pagina) {
    EnterCriticalSection(&pool->mutex_cache);

    // 1. Procura se a página já está mapeada na RAM (Cache Hit)
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (pool->frames[i].id_pagina == id_pagina) {
            pool->frames[i].pin_count++;
            mover_para_cabeca_lru(pool, &pool->frames[i]);
            LeaveCriticalSection(&pool->mutex_cache);
            return &pool->frames[i];
        }
    }

    // 2. Procura um slot completamente livre
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (pool->frames[i].id_pagina == DESLOCAMENTO_NULO) {
            pool->frames[i].id_pagina = id_pagina;
            pool->frames[i].pin_count = 1;
            pool->frames[i].dirty = false;
            ler_pagina(arquivo_db, id_pagina, &pool->frames[i].pagina);
            
            // Registra na lista LRU
            if (!pool->lru_head) {
                pool->lru_head = &pool->frames[i];
                pool->lru_tail = &pool->frames[i];
            } else {
                pool->frames[i].next = pool->lru_head;
                pool->lru_head->prev = &pool->frames[i];
                pool->lru_head = &pool->frames[i];
            }
            LeaveCriticalSection(&pool->mutex_cache);
            return &pool->frames[i];
        }
    }

    // 3. Cache Miss & Sem slots vazios: Varre a lista LRU de trás para frente buscando despejo (Eviction)
    FrameBuffer* vitima = pool->lru_tail;
    while (vitima != NULL) {
        if (vitima->pin_count == 0) {
            break; // Candidato ideal encontrado! Não está em uso por nenhuma thread
        }
        vitima = vitima->prev;
    }

    // Se TODAS as páginas estiverem pinadas (Thread starvation/falta de unpin no código)
    if (!vitima) {
        printf("CRITICAL ERROR: Buffer Pool esgotado! Todas as paginas estao pinadas.\n");
        exit(1);
    }

    // Se a página vítima foi alterada, força o flush sincronizado em disco
    if (vitima->dirty) {
        escrever_pagina(arquivo_db, vitima->id_pagina, &vitima->pagina);
    }

    // Substitui a página antiga pela nova
    vitima->id_pagina = id_pagina;
    vitima->pin_count = 1;
    vitima->dirty = false;
    ler_pagina(arquivo_db, id_pagina, &vitima->pagina);

    // Atualiza a posição dela para o topo de uso
    mover_para_cabeca_lru(pool, vitima);

    LeaveCriticalSection(&pool->mutex_cache);
    return vitima;
}

void unpin_pagina(BufferPool* pool, int64_t id_pagina, bool foi_modificada) {
    EnterCriticalSection(&pool->mutex_cache);
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (pool->frames[i].id_pagina == id_pagina) {
            if (foi_modificada) {
                pool->frames[i].dirty = true;
            }
            if (pool->frames[i].pin_count > 0) {
                pool->frames[i].pin_count--;
            }
            break;
        }
    }
    LeaveCriticalSection(&pool->mutex_cache);
}

void flush_buffer_pool(BufferPool* pool, FILE* arquivo_db) {
    EnterCriticalSection(&pool->mutex_cache);
    for (int i = 0; i < MAX_FRAMES; i++) {
        if (pool->frames[i].id_pagina != DESLOCAMENTO_NULO && pool->frames[i].dirty) {
            escrever_pagina(arquivo_db, pool->frames[i].id_pagina, &pool->frames[i].pagina);
            pool->frames[i].dirty = false;
        }
    }
    LeaveCriticalSection(&pool->mutex_cache);
}

void destruir_buffer_pool(BufferPool* pool, FILE* arquivo_db) {
    flush_buffer_pool(pool, arquivo_db);
    DeleteCriticalSection(&pool->mutex_cache);
}