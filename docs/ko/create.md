---
title: 만들기
description: Ampify용 테마, 스니펫, 확장 기능을 만들고, Apple Music에서 바로 확인하고, 카탈로그에 공유하기.
---

# Ampify용으로 만들기

테마는 몇 개의 텍스트 파일이 든 폴더입니다. 확장 기능은 JavaScript 파일(또는 대신 컴파일되는 TypeScript)과
매니페스트입니다. Ampify가 설치되어 있고, Apple Music이 있고, 작업 폴더에서 연 터미널이 있으면 됩니다. 컴파일할
것도, 만들 계정도 없습니다. 자세한 문서는 영어로 되어 있습니다.

1. **잘 작동하는 것에서 시작하기.**
   `ampify new theme my-theme`(또는 `extension`, `snippet`, `app`)은 완전한 출발점을 현재 폴더에 만듭니다.
   이미 작동하므로 빈 페이지에서 시작하는 대신 잘 되는 것을 고쳐 나가면 됩니다.

2. **바로 확인하기.**
   `ampify dev my-theme`은 이를 Ampify에 복사해 켜고, 저장할 때마다 다시 불러옵니다. Apple Music은 다시 시작하지
   않아도 1초 안에 바뀝니다. 터미널은 열어 두세요.

3. **꾸밀 대상 찾기.**
   테마는 Apple Music의 각 영역에 붙은 고정된 이름을 가리킵니다. `.sidebar`, `.playbar`, `.lcd`, `.track-row`,
   `.card` 등 백 개가 넘습니다([전체 목록](../themes/names.md)). 나머지는 `ampify devtools`를 실행한 뒤
   `ampify inspect`를 쓰면 인터페이스 트리를 실시간으로 보여 주며, 각 요소의 선택자도 알 수 있습니다.

4. **모양 잡기.**
   색은 `colors.ini`에 두며, 색상 구성표마다 섹션 하나를 씁니다. 형태, 간격, 마우스를 올렸을 때의 모습, 전환,
   애니메이션은 이미 아시는 CSS로 `user.css`에 씁니다. 테마는 자체 설정(슬라이더, 스위치, 선택지)을 선언할 수 있고,
   사용자는 Apple Music 설정에서 이를 바꿉니다. [테마 형식](../themes/format.md)을 참고하세요.

5. **또는 동작 작성하기.**
   확장 기능은 `Ampify` API를 씁니다. 플레이어, 곡, 알림, 메뉴, 패널, 저장소, 그리고 매니페스트에 선언한 웹 호출을
   다룰 수 있습니다. 각 권한은 설치 전에 표시됩니다. [확장 기능 가이드](../extensions/guide.md)와
   [API](../extensions/api.md)를 참고하세요.

6. **검증하기.**
   `ampify validate my-theme`은 카탈로그와 같은 규칙으로 매니페스트와 파일을 검사합니다.

7. **공유하기.**
   `ampify publish my-theme`은 한 번 더 검증한 뒤, 아무것도 복제하지 않고 [공개 카탈로그](https://github.com/Shirowwww/ampify-catalog)에
   풀 리퀘스트를 대신 열어 줍니다. 승인되면 모든 Ampify의 Marketplace와 [갤러리](themes.html)에 나타납니다.
{: .steps}

## 한 가지는 한 가지 방식으로

테마, 스니펫, 확장 기능은 도구를 함께 씁니다. 두 종류가 같은 일을 할 수 있다면 같은 방식으로 합니다. 하나에서
배운 것은 다른 것에도 그대로 쓸 수 있습니다.

| 하려는 일 | 사용할 것 | 대상 |
|---|---|---|
| 설정 제공하기 | 매니페스트의 `settings` 필드([매니페스트](../extensions/manifest.md#settings)). CSS는 `@setting`과 `--setting-<키>`로, JavaScript는 `Ampify.config.get(키)`로 읽습니다 | 테마, 스니펫, 확장 기능, 앱 |
| 이름, 설명, 설정 번역하기 | 매니페스트의 `translations` 필드 | 모든 항목 |
| 재생 막대의 위치 알기 | `@setting bar-position`. 사용자가 모든 테마에 정하거나, 테마의 `"playbar"`가 정합니다 | 테마, 스니펫 |
| 패널의 모서리 따라가기 | `--panel-radius`(막대와 디스플레이에는 `--bar-radius`, `--lcd-radius`) | 테마와 스니펫이 정하고, 스니펫과 확장 기능의 패널이 따라갑니다 |
| 현재 앨범 아트의 색 가져오기 | CSS에서는 `--art-1`부터 `--art-3`와 그 변형, JavaScript에서는 `Ampify.color.playing()` | 모두 |
| 음악을 따라 움직이는 막대 그리기 | `content: bars(n, 모양, 높이)` | 테마, 스니펫 |
| 요소를 흐리게 하거나, 색을 바꾸거나, 유리 효과 입히기 | `filter`와 `backdrop` | 테마, 스니펫 |
| 행, 스위치, 컨트롤을 상태에 따라 꾸미기 | `:hover`, `:active`, `:focus`, `:current`, `:playing`, `:checked`, `:selected`, `:disabled` | 테마, 스니펫 |
| 진행선의 한 구간 표시하기 | `Ampify.ui.playbar.mark(id, from, to)` | 확장 기능 |
| 인터페이스 만들기 | `Ampify.ui`의 노드, 또는 `Ampify.React`와 `Ampify.components`(버튼, 스위치, 체크 상자, 슬라이더, 입력란, 메뉴, 탭, 진행 막대, 로딩 표시, 구분선 등) | 확장 기능, 앱 |

[테마 형식](../themes/format.md#what-a-theme-tells-snippets-and-extensions)에는 테마가 다른 항목에 전달하는 것이 나와
있고, [API](../extensions/api.md#55-interface-stable)에는 모든 노드와 구성 요소가 나와 있습니다.

## Spicetify에서 오셨다면

Spicetify의 `color.ini`는 그대로 작동합니다. 테마 폴더에 넣으면 그 색이 Apple Music에 입혀집니다. `user.css`는
Spotify의 클래스 대신 Ampify의 이름을 써야 합니다.
[되는 것과 안 되는 것](../themes/spicetify.md)을 참고하세요.

## 라이선스

Ampify는 PolyForm Noncommercial 1.0.0 라이선스를 따릅니다. 출처를 밝히는 한 상업적 용도를 제외한 모든 용도로
자유롭게 쓸 수 있습니다. 이 라이선스는 여러분이 만든 것에는 적용되지 않습니다. Ampify가 문서로 공개한 형식과 API만
쓰는 테마, 스니펫, 확장 기능, 앱은 여러분의 것이며, 상업용이든 아니든 원하는 라이선스로 낼 수 있습니다.
`ampify new`의 출발점과 예제는 MIT 라이선스이니 원하는 만큼 가져다 쓰세요.

## 더 알아보기

- [5분 만에 테마 만들기](../themes/guide.md)
- [테마 형식: 모든 속성과 선택자](../themes/format.md)
- [Apple Music 인터페이스의 고정된 이름](../themes/names.md)
- [확장 기능 작성하기](../extensions/guide.md)와 [그 매니페스트](../extensions/manifest.md)
- [명령줄](../cli.md)
