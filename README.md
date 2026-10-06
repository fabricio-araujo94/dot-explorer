# Dot Explorer

Gerenciador de arquivos para terminal, escrito em C com `ncurses`. A interface apresenta dois painéis lado a lado para navegação e operações com arquivos.

> O programa ainda está em desenvolvimento. Faça cópias de segurança dos arquivos importantes antes de usar operações de exclusão, movimentação ou cópia.

## Recursos

- Navegação entre diretórios e histórico por painel.
- Filtro incremental por nome.
- Seleção múltipla, copiar, recortar, colar, renomear, criar e excluir.
- Ordenação por nome, tamanho e data.
- Cópia de arquivo único em segundo plano, com cancelamento por `Esc`.

## Atalhos

| Tecla | Ação |
| --- | --- |
| `Tab` | Alternar o painel ativo |
| `k` / `↑` | Mover seleção para cima |
| `j` / `↓` | Mover seleção para baixo |
| `Enter` / `→` | Abrir diretório ou arquivo selecionado |
| `h` / `←` / `Backspace` | Voltar ao diretório pai |
| `Ctrl+O` | Voltar no histórico do painel ativo |
| `Ctrl+]` | Avançar no histórico do painel ativo |
| `r` | Atualizar o diretório |
| `/` | Filtrar entradas por nome |
| `Space` | Marcar ou desmarcar a entrada selecionada |
| `c` / `x` / `p` | Copiar / recortar / colar |
| `d` / `R` | Excluir / renomear entrada |
| `n` / `N` | Criar arquivo / diretório |
| `s` / `S` / `D` | Ordenar por nome / tamanho / data |
| `i` | Exibir propriedades |
| `q` | Sair |
| `Esc` | Cancelar uma cópia em andamento |

O painel ativo é realçado. As operações de copiar, recortar e colar usam a seleção e o diretório do painel ativo.

## Requisitos e Compilação

Requisitos: GCC ou Clang, GNU Make, `ncursesw` e suporte a pthreads.

No Debian ou Ubuntu:

```sh
sudo apt-get install build-essential libncurses-dev
make
./dot-explorer
```

No Windows, use o ambiente **MSYS2 MinGW-w64** com GCC, Make, ncurses e pthreads instalados. Abra o terminal MinGW-w64, execute `make` e inicie o executável produzido pelo ambiente. O workflow de CI deste repositório valida Linux com GCC e Clang; Windows não é coberto pela CI.

## Organização dos Headers

Headers de implementação e contratos entre módulos ficam em `src/` e são internos ao aplicativo. `include/` fica reservado para uma futura API pública destinada a consumidores externos; atualmente o projeto não expõe essa API.
