---
title: Primeiros passos
description: Instalar o Ampify, escolher um tema, voltar ao Apple Music original e o que fazer quando o Ampify se desliga depois de uma atualização do Apple Music.
---

# Primeiros passos com o Ampify

O Ampify acrescenta temas, extensões e apps ao Apple Music para Windows. Ele nunca altera os arquivos do Apple Music:
se você o desinstalar, o app original volta. A documentação detalhada está em inglês.

## O que você precisa

- Windows 10 ou 11 (x64), com o Apple Music instalado pela Microsoft Store e com login feito. O Ampify nunca pede
  seu ID Apple.
- Uma versão testada do Apple Music. Com outra, o Apple Music abre normalmente e o Ampify fica desligado (veja mais
  abaixo).

## Instalar

Feche o Apple Music antes, inclusive na área de notificação ao lado do relógio, onde ele pode ficar escondido.

**Com o zip.** Baixe `ampify-<version>-windows-x64.zip` na página [Releases](https://github.com/Shirowwww/ampify/releases),
extraia o zip inteiro (clique com o botão direito, Extrair tudo) e dê um duplo clique em `ampify_setup.exe` na pasta nova.
Se for aberto de dentro do zip, o instalador avisa que falta um arquivo. Se o Windows mostrar "O Windows protegeu seu computador",
clique em "Mais informações" e depois em "Executar assim mesmo": os arquivos ainda não são
assinados.

**Com o PowerShell.** Abra-o (menu Iniciar, digite "PowerShell") e cole:

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
```

Depois, abra o Apple Music como sempre, pelo ícone dele: não existe atalho especial.

## Escolher um tema

Logo depois da instalação, o Apple Music fica igual ao que era: o Ampify não traz nenhum tema. O primeiro vem do
Marketplace.

1. Clique em **Ampify**, no fim da barra lateral. O Marketplace abre.
2. Em **Temas** (precisa de internet), clique em **Instalar** no que você quiser, por exemplo Glass ou
   Artwork: ele passa a valer na hora.
3. Escolha uma variante no menu se o tema tiver várias, por exemplo Dark ou Light.

A mudança é imediata. **Desativar** (no tema em uso) e **Usar o visual do Apple Music** (no fim da
lista) devolvem a aparência original. Um tema já instalado continua funcionando sem internet.

As palavras usadas: um **tema** muda a aparência do Apple Music, uma **extensão** acrescenta um recurso, um **app**
é uma página à parte com uma entrada na barra lateral e um **snippet** são algumas linhas de estilo ligadas além de
um tema. A aba **Desenvolvedores** reúne os exemplos para quem cria extensões. A **variante** é a versão clara ou
escura que um tema pode oferecer.

A posição da barra de reprodução vale uma vez para todos os temas, nas configurações do Ampify, aba
Aparência, em "Barra de reprodução", abaixo do tema. "Conforme o tema" mantém a posição de cada tema; No topo, Embaixo
e "Embaixo, progresso sob os botões" (a linha passa sob os botões, entre o tempo decorrido e a duração) valem
para todos.

## Voltar ao Apple Music

Três jeitos, do mais leve ao mais completo:

1. **Só o tema.** No Marketplace, **Desativar** no tema em uso, ou **Usar o visual do Apple Music**.
2. **Ampify desligado por um tempo**, sem remover nada. Ainda não há botão: num terminal, `ampify restore`
   desliga e `ampify apply` liga de novo.
3. **Desinstalar.** `ampify uninstall` num terminal, ou Configurações > Aplicativos > Aplicativos instalados >
   "Ampify for Apple Music". Os dois abrem o mesmo desinstalador, que fecha o Apple Music se ele estiver aberto. Uma
   caixa, desmarcada de início, apaga também seus temas, extensões e configurações em `%USERPROFILE%\Ampify`.
   `ampify uninstall --yes` desinstala sem janela e mantém seus arquivos (`--delete-data` apaga).

## Quando o Apple Music se atualiza

O Apple Music se atualiza sozinho pela Microsoft Store. O Ampify só carrega nas versões que testou, então
depois de uma atualização ele pode ficar desligado: o Apple Music abre como sempre, sem o seu tema, e nada está
quebrado. Ele também se desliga por uma abertura quando o Apple Music fechou sozinho duas vezes seguidas.

O Windows mostra então uma notificação, uma só vez por versão: "O Ampify está desligado por enquanto" (ou "O Ampify
pulou esta abertura"). Se ela disser que o Ampify recebeu a nova lista, feche o Apple Music por completo (também na
área de notificação) e abra-o de novo. Se não, o Ampify volta quando a nova versão for testada: uma nova
versão do Ampify traz isso (as atualizações se instalam sozinhas), e `ampify versions update` procura a lista
na hora. `ampify doctor` mostra o mesmo estado a qualquer momento.

## Quando o Ampify se atualiza

Seus temas, snippets e extensões acompanham. Na primeira abertura de uma versão nova, o Ampify leva cada elemento
do catálogo para a versão nova e troca um snippet retirado pelo que o substituiu, com as suas configurações. Sem
internet, eles ficam como estão e a abertura seguinte tenta de novo. `ampify update --elements` faz a mesma coisa à
mão.

## Algo deu errado

1. Rode `ampify doctor`: ele verifica o Ampify e lista cada problema.
2. Rode `ampify report`: ele grava um zip anonimizado com o estado e os registros em `%USERPROFILE%\Ampify\reports`
   (o botão **Exportar um relatório** da página de configurações do Ampify faz o mesmo). Dê uma olhada e anexe a
   uma [issue](https://github.com/Shirowwww/ampify/issues).
