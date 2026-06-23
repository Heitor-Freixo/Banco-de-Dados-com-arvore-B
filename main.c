#include <stdio.h>
#include <assert.h>
#include "storage.h"

int main(int argc, char* argv[]) {
    if (argc > 1) {
        printf("Executando testes de storage com nomes internos em português...\n");
        
        bool eh_novo;
        FILE* db = inicializar_armazenamento("banco_teste.bin", &eh_novo);
        assert(db != NULL);
        
        SuperBloco sb;
        assert(ler_superbloco(db, &sb) == true);
        
        int64_t p1 = alocar_pagina(db, &sb);
        int64_t p2 = alocar_pagina(db, &sb);
        assert(p1 == 1);
        assert(p2 == 2);
        
        PaginaBTree pagina_ficticia;
        pagina_ficticia.cabecalho.qtd_chaves = 2;
        pagina_ficticia.cabecalho.eh_folha = 1;
        pagina_ficticia.chaves[0] = 202601; 
        pagina_ficticia.chaves[1] = 202602;
        pagina_ficticia.deslocamentos_registros[0] = 5000; 
        pagina_ficticia.deslocamentos_registros[1] = 5500;
        
        assert(escrever_pagina(db, p1, &pagina_ficticia) == true);
        
        PaginaBTree pagina_lida;
        assert(ler_pagina(db, p1, &pagina_lida) == true);
        assert(pagina_lida.cabecalho.qtd_chaves == 2);
        assert(pagina_lida.chaves[0] == 202601);
        assert(pagina_lida.chaves[1] == 202602);
        
        liberar_id_pagina(&sb, p1); 
        int64_t p3 = alocar_pagina(db, &sb);
        assert(p3 == 1); 
        
        fclose(db);
        printf("Todos os testes passaram!\n");
        return 0;
    }

    printf("Use 'make test' para rodar os testes.\n");
    return 0;
}