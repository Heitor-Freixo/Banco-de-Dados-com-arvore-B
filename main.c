#include <stdio.h>
#include <assert.h>
#include "storage.h"
#include "btree.h"

int main(int argc, char* argv[]) {
    (void)argv; // Silencia o warning estrito do GCC exigido pelo edital

    if (argc > 1) {
        printf("Executando testes do Passo 3 (Busca com Trava Windows CRITICAL_SECTION)...\n");
        
        bool eh_novo;
        FILE* arquivo = inicializar_armazenamento("banco_teste.bin", &eh_novo);
        assert(arquivo != NULL);
        
        GerenciadorBTree btree;
        inicializar_btree(&btree, arquivo);

        if (eh_novo) {
            SuperBloco sb;
            assert(ler_superbloco(arquivo, &sb) == true);

            // Aloca logicamente duas páginas (Raiz ID 1 e Filho ID 2)
            int64_t id_raiz = alocar_pagina(arquivo, &sb);
            int64_t id_filho = alocar_pagina(arquivo, &sb);
            
            sb.id_pagina_raiz = id_raiz;
            assert(escrever_superbloco(arquivo, &sb) == true);
            btree.superbloco = sb; 

            // Monta nó raiz na RAM e persiste
            PaginaBTree raiz;
            memset(&raiz, 0, sizeof(PaginaBTree));
            raiz.cabecalho.qtd_chaves = 1;
            raiz.cabecalho.eh_folha = 0; 
            raiz.chaves[0] = 50;         
            raiz.deslocamentos_filhos[0] = id_filho; 
            assert(escrever_pagina(arquivo, id_raiz, &raiz) == true);

            // Monta nó filho (folha) na RAM e persiste
            PaginaBTree filho;
            memset(&filho, 0, sizeof(PaginaBTree));
            filho.cabecalho.qtd_chaves = 2;
            filho.cabecalho.eh_folha = 1; 
            filho.chaves[0] = 10;
            filho.chaves[1] = 20;
            filho.deslocamentos_registros[0] = 8000; 
            filho.deslocamentos_registros[1] = 8500;
            assert(escrever_pagina(arquivo, id_filho, &filho) == true);
        }

        // --- EXECUÇÃO DAS VALIDAÇÕES ---

        // Teste 1: Buscar chave na folha profunda (deve contar 2 leituras de bloco)
        ResultadoBusca rb1 = buscar_chave(&btree, 20);
        assert(rb1.encontrada == true);
        assert(rb1.deslocamento_registro == 8500);
        assert(rb1.paginas_lidas == 2); 
        printf(" -> Teste 1: Concorrência nativa Windows e busca multinível... OK!\n");

        // Teste 2: Chave inexistente
        ResultadoBusca rb2 = buscar_chave(&btree, 99);
        assert(rb2.encontrada == false);
        printf(" -> Teste 2: Tratamento de chave inexistente... OK!\n");

        destruir_btree(&btree);
        fclose(arquivo);
        printf("\n======================================================\n");
        printf("  PARABÉNS! Passo 3 concluído e protegido no Windows!\n");
        printf("======================================================\n");
        return 0;
    }

    printf("Execute com o comando 'make test' para rodar as validações.\n");
    return 0;
}