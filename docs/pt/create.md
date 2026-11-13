---
title: Criar
description: Faça um tema, um snippet ou uma extensão para o Ampify, veja ao vivo no Apple Music e compartilhe no catálogo.
---

# Criar para o Ampify

Um tema é uma pasta com alguns arquivos de texto. Uma extensão é um arquivo JavaScript (ou TypeScript, compilado para você) e um manifesto. Você precisa do
Ampify instalado, do Apple Music e de um terminal aberto numa pasta de trabalho. Nada para compilar, nenhuma conta para
criar. A documentação detalhada está em inglês.

1. **Comece com um elemento que funciona.**
   `ampify new theme meu-tema` (ou `extension`, `snippet`, `app`) escreve um modelo completo na pasta atual.
   Ele já roda: você muda algo que funciona, em vez de começar de uma página em branco.

2. **Veja ao vivo.**
   `ampify dev meu-tema` copia o tema para o Ampify, ativa e recarrega a cada vez que você salva. O Apple Music muda em
   menos de um segundo, sem reiniciar. Deixe o terminal aberto.

3. **Descubra o que estilizar.**
   Os temas usam nomes estáveis para as áreas do Apple Music: `.sidebar`, `.playbar`, `.lcd`, `.track-row`, `.card` e mais de
   cem outros ([a lista completa](../themes/names.md)). Para o resto, `ampify devtools` e depois `ampify inspect`
   mostram a árvore da interface ao vivo, com o seletor de cada elemento.

4. **Dê forma.**
   As cores vão em `colors.ini`, uma seção por esquema de cores. Formas, espaços, efeitos ao passar o ponteiro, transições
   e animações vão em `user.css`, com o CSS que você já conhece. Um tema pode declarar suas próprias configurações (um
   controle deslizante, um interruptor, uma escolha), que cada pessoa muda nas configurações do Apple Music. Veja
   [o formato dos temas](../themes/format.md).

5. **Ou escreva um comportamento.**
   As extensões usam a API `Ampify`: o player, a música, as notificações, os menus, os painéis, o armazenamento e as
   chamadas à web que o manifesto declara. Cada permissão aparece antes da instalação. Veja
   [o guia de extensões](../extensions/guide.md) e [a API](../extensions/api.md).

6. **Confira.**
   `ampify validate meu-tema` verifica o manifesto e os arquivos com as mesmas regras do catálogo.

7. **Compartilhe.**
   `ampify publish meu-tema` verifica de novo e abre para você um pull request no
   [catálogo público](https://github.com/Shirowwww/ampify-catalog), sem clonar nada. Depois de aceito,
   ele aparece no Marketplace de cada Ampify e na [galeria](themes.html). O caminho completo, com a prévia, cabe
   em [cinco passos](../extensions/catalog.md#propose-an-element-in-five-steps).
{: .steps}

## Um só jeito para cada coisa

Temas, snippets e extensões compartilham as mesmas ferramentas. Quando dois tipos de elemento fazem a mesma coisa, fazem do
mesmo jeito: o que você aprende com um vale para os outros.

| Para... | Use | Vale em |
|---|---|---|
| Oferecer configurações | O campo `settings` do manifesto ([manifesto](../extensions/manifest.md#settings)): o CSS lê com `@setting` e `--setting-<key>`, o JavaScript com `Ampify.config.get(key)` | Temas, snippets, extensões, apps |
| Traduzir o nome, a descrição e as configurações | O campo `translations` do manifesto | Todos os elementos |
| Saber onde fica a barra de reprodução | `@setting bar-position`, escolhido pelo usuário para todos os temas, ou o `"playbar"` do tema | Temas, snippets |
| Seguir os cantos dos painéis | `--panel-radius` (e `--bar-radius`, `--lcd-radius` para a barra e o visor) | Temas e snippets definem; snippets e painéis de extensões seguem |
| Usar as cores da capa que está tocando | `--art-1` a `--art-3` e suas variantes no CSS, `Ampify.color.playing()` no JavaScript | Todos |
| Desenhar barras que se mexem com a música | `content: bars(n, shape, height)` | Temas, snippets |
| Desfocar, recolorir ou pôr vidro num elemento | `filter` e `backdrop` | Temas, snippets |
| Estilizar uma linha, um interruptor ou um controle conforme o estado | `:hover`, `:active`, `:focus`, `:current`, `:playing`, `:checked`, `:selected`, `:disabled` | Temas, snippets |
| Marcar um trecho da linha de progresso | `Ampify.ui.playbar.mark(id, from, to)` | Extensões |
| Montar uma interface | Os nós de `Ampify.ui`, ou `Ampify.React` com `Ampify.components` (botões, interruptores, caixas de seleção, controles deslizantes, campos, menus, abas, barras de progresso, indicadores de carregamento, divisores...) | Extensões, apps |

[O formato dos temas](../themes/format.md#what-a-theme-tells-snippets-and-extensions) lista o que um tema passa aos outros
elementos, e [a API](../extensions/api.md#55-interface-stable) lista cada nó e cada componente.

## Vindo do Spicetify

Um `color.ini` do Spicetify funciona como está: colocado na pasta de um tema, suas cores vestem o Apple Music. O
`user.css` precisa dos nomes do Ampify no lugar das classes do Spotify.
[O que passa e o que não passa](../themes/spicetify.md).

## Licenças

O Ampify usa a licença PolyForm Noncommercial 1.0.0: livre para qualquer uso, menos o comercial, mantendo o crédito.
Essa licença não alcança o que você cria: temas, snippets, extensões e apps que só usam os formatos e a API documentados
do Ampify são seus, sob a licença que você escolher, comercial ou não. Os modelos do `ampify new` e os exemplos usam a
licença MIT: copie deles o que quiser.

## Ir além

- [Um tema em cinco minutos](../themes/guide.md)
- [O formato dos temas: cada propriedade e seletor](../themes/format.md)
- [Os nomes estáveis da interface do Apple Music](../themes/names.md)
- [Escrever uma extensão](../extensions/guide.md) e [seu manifesto](../extensions/manifest.md)
- [A linha de comando](../cli.md)
