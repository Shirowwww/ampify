---
title: 시작하기
description: Ampify 설치, 테마 고르기, 원래의 Apple Music으로 돌아가기, 그리고 Apple Music 업데이트 후 Ampify가 꺼졌을 때 할 일.
---

# Ampify 시작하기

Ampify는 Windows용 Apple Music에 테마, 확장 기능, 앱을 더합니다. Apple Music의 파일은 절대 수정하지 않으며,
Ampify를 제거하면 원래의 앱 그대로 돌아옵니다. 자세한 문서는 영어로 되어 있습니다.

## 필요한 것

- Windows 10 또는 11(x64), 그리고 Microsoft Store에서 설치하고 로그인해 둔 Apple Music. Ampify는 Apple ID를 절대
  요구하지 않습니다.
- 테스트를 거친 Apple Music 버전. 다른 버전에서는 Apple Music이 평소처럼 열리고 Ampify는 꺼진 채로 있습니다(아래
  참고).

## 설치하기

먼저 Apple Music을 닫으세요. 시계 옆 알림 영역에 숨어 있을 수 있으니 그곳에서도 종료해야 합니다.

**zip 파일로.** [Releases](https://github.com/Shirowwww/ampify/releases) 페이지에서 `ampify-<version>-windows-x64.zip`을
내려받고, zip 전체의 압축을 풉니다(마우스 오른쪽 버튼, 모두 압축 풀기). 그런 다음 새로 생긴 폴더에서 `ampify_setup.exe`를
두 번 클릭하세요. zip 안에서 바로 실행하면 설치 프로그램이 파일이 없다고 알려 줍니다. Windows에 "Windows의 PC 보호"가
표시되면 "추가 정보"를 누른 다음 "실행"을 누르세요. 파일에 아직 서명이 없기 때문입니다.

**PowerShell로.** PowerShell을 열고(시작 메뉴에서 "PowerShell" 입력) 다음을 붙여 넣으세요.

```powershell
irm https://raw.githubusercontent.com/Shirowwww/ampify/main/scripts/install.ps1 | iex
```

그다음에는 평소처럼 Apple Music 자체 아이콘으로 여세요. 따로 만든 바로 가기는 없습니다.

## 테마 고르기

설치 직후의 Apple Music은 이전과 똑같습니다. Ampify에는 테마가 들어 있지 않고, 첫 테마는 Marketplace에서 받습니다.

1. 사이드바 맨 아래의 **Ampify**를 클릭하세요. Marketplace가 열립니다.
2. **테마**에서(인터넷 필요) 마음에 드는 테마의 **설치**를 클릭하세요. 예를 들어 Glass나 Artwork를 고르면 바로
   사용됩니다.
3. 테마에 변형이 여러 개 있으면 메뉴에서 하나를 고르세요. 예를 들어 Dark나 Light입니다.

변경은 바로 적용됩니다. (사용 중인 테마의) **끄기**와 (목록 맨 아래의) **Apple Music 기본 모양 사용**을 누르면 원래
모양으로 돌아옵니다. 이미 설치한 테마는 인터넷이 없어도 계속 작동합니다.

용어를 정리하면, **테마**는 Apple Music의 모양을 바꾸고, **확장 기능**은 기능을 더하고, **앱**은 사이드바에
항목이 생기는 별도 페이지이고, **스니펫**은 테마와 함께 켜는 몇 줄의 스타일입니다. **개발자용** 탭에는 확장 기능을
만드는 분을 위한 예제가 모여 있습니다. **변형**은 테마가 제공할 수 있는 밝은 버전 또는 어두운 버전입니다.

재생 막대의 위치는 Ampify 설정의 "재생 막대"에서 모든 테마에 한 번에 정합니다. "테마 기본값"은
각 테마가 정한 위치를 그대로 쓰고, "위쪽", "아래쪽", "아래쪽, 가운데"는 모든 테마에 적용됩니다.

## Apple Music으로 돌아가기

가벼운 것부터 완전한 것까지 세 가지 방법이 있습니다.

1. **테마만.** Marketplace에서 사용 중인 테마의 **끄기**, 또는 **Apple Music 기본 모양 사용**을 누르세요.
2. **Ampify를 잠시 끄기.** 아무것도 삭제되지 않습니다. 아직 버튼은 없고, 터미널에서 `ampify restore`로 끄고
   `ampify apply`로 다시 켭니다.
3. **제거하기.** 터미널에서 `ampify uninstall`을 실행하거나, 설정 > 앱 > 설치된 앱 >
   "Ampify for Apple Music"을 사용하세요. 둘 다 같은 제거 프로그램을 열며, Apple Music이 열려 있으면 닫습니다.
   처음에는 선택되어 있지 않은 확인란을 선택하면 `%USERPROFILE%\Ampify`의 테마, 확장 기능, 설정도 함께 삭제됩니다.
   `ampify uninstall --yes`는 창 없이 제거하며 파일은 남겨 둡니다(`--delete-data`를 붙이면 삭제).

## Apple Music이 업데이트될 때

Apple Music은 Microsoft Store를 통해 저절로 업데이트됩니다. Ampify는 테스트를 거친 버전에서만 로드되므로 업데이트
후에는 꺼진 채로 있을 수 있습니다. 그럴 때 Apple Music은 테마 없이 평소처럼 열리며, 고장 난 것은 없습니다.
Apple Music이 연달아 두 번 저절로 종료된 경우에도 한 번은 시작을 건너뜁니다.

그러면 Windows가 버전마다 한 번씩 알림을 표시합니다. "Ampify가 잠시 꺼져 있습니다"(또는 "Ampify가 이번 시작을
건너뛰었습니다")입니다. Ampify가 새 목록을 받았다고 나오면 Apple Music을 완전히 종료한(알림 영역에서도) 뒤 다시
여세요. 그렇지 않으면 새 버전의 테스트가 끝났을 때 Ampify가 돌아옵니다. 새 Ampify 버전이 그 테스트를 가져오며(업데이트는
저절로 설치됩니다), `ampify versions update`는 목록을 바로 찾아봅니다. `ampify doctor`는 언제든 같은 상태를
보여 줍니다.

## 문제가 생겼을 때

1. `ampify doctor`를 실행하세요. Ampify를 점검하고 문제를 모두 나열합니다.
2. `ampify report`를 실행하세요. 개인 정보를 가린 상태와 로그의 zip을 `%USERPROFILE%\Ampify\reports`에 만듭니다
   (Ampify 설정 페이지의 **보고서 내보내기** 버튼도 같은 일을 합니다). 내용을 확인한 뒤
   [이슈](https://github.com/Shirowwww/ampify/issues)에 첨부하세요.
