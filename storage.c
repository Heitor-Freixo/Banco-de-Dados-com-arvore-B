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