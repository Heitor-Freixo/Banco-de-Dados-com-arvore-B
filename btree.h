#ifndef BTREE_H
#define BTREE_H

#include "storage.h"
#include <windows.h> // Biblioteca nativa do Windows para concorrência e exclusão mútua

// Estrutura de controle da Árvore B em memória
typedef struct {
    FILE* arquivo_db;
    SuperBloco superbloco;
    CRITICAL_SECTION mutex_banco; // Seção crítica nativa do Windows (Mutex Global)
} GerenciadorBTree;

// Estrutura para retornar o resultado completo da busca com estatísticas de I/O
typedef struct {
    deslocamento_disco_t deslocamento_registro; // Posição física do registro no arquivo
    int64_t id_pagina_onde_esta;                // ID do nó onde a chave foi encontrada
    int32_t indice_na_pagina;                   // Índice da chave dentro do nó
    int32_t paginas_lidas;                      // Contador de páginas lidas (para o EXPLAIN)
    bool encontrada;                            // Identificador de sucesso da busca
} ResultadoBusca;

// Assinaturas das funções da Árvore B
void inicializar_btree(GerenciadorBTree* btree, FILE* arquivo);
void destruir_btree(GerenciadorBTree* btree);
ResultadoBusca buscar_chave(GerenciadorBTree* btree, int32_t chave_matricula);

#endif // BTREE_H