# Dot Explorer

Um gerenciador de arquivos para terminal rápido, leve e moderno, escrito em C usando `ncurses`.

> ATENÇÃO: este programa não foi suficientemente testado e a execução de certas funcionalidades podem ter efeitos diversos. Recomenda-se cautela e o uso num ambiente isolado.

## Recursos

- Navegação por teclado.
- Carregamento e exibição rápidos de diretórios.
- Operações básicas com arquivos (criar, excluir, renomear).
- Seleção múltipla.

## Requisitos

- `gcc` ou `clang`
- `make`
- Biblioteca `ncurses` (por exemplo, `libncurses-dev` no Debian/Ubuntu, `ncurses` no Arch ou disponível via MSYS2 no Windows).

## Compilação

```bash
make
./dot-explorer
```
