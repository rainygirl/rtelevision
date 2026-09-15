# <img src="resources/icon/icon_64.png" width="32" height="32" alt=""> R Television

전 세계 무료 TV 스트리밍 플레이어입니다. 왼쪽에 채널, 오른쪽에 화면. 화면 언어는 시스템
설정을 따라갑니다(영어, 이탈리아어, 일본어, 한국어).

[English](README.md) · [Italiano](README.it.md) · [日本語](README.ja.md) · 한국어

## 준비물

| 시스템 | 설치 방법 | 그 밖에 필요한 것 |
|---|---|---|
| macOS 11 이상, Apple Silicon 또는 Intel | 앱 다운로드 | 없음 |
| Linux x86_64: Debian, Ubuntu, Linux Mint | 명령 한 줄로 빌드 | 인터넷 연결, 비밀번호 한 번 |
| Haiku x86_64 | 명령 한 줄로 빌드 | 인터넷 연결 |
| Haiku arm64 | 명령 한 줄로 빌드 | 미리 준비한 FFmpeg([Haiku](#haiku) 참고) |

어느 시스템에서도 VLC를 따로 설치할 필요가 없습니다. R Television이 직접 들고 다닙니다.

## 설치

### macOS

![macOS의 R Television](docs/screenshots/macos.png)

1. `R-Television-1.0.0-macOS.dmg`를 다운로드합니다.
2. 열어서 **R Television**을 **응용 프로그램** 폴더로 끌어다 놓습니다.
3. 응용 프로그램 폴더에서 R Television을 엽니다.

Apple Developer ID로 서명한 앱이 아니어서 처음에는 macOS가 실행을 막습니다. 아래 방법 중
편한 것으로 한 번만 허용하면 됩니다.

- **macOS 15 이상:** 경고가 뜬 뒤 **시스템 설정 > 개인정보 보호 및 보안**을 열고
  *"R Television"이(가) 차단되었습니다* 항목까지 내려가 **그래도 열기**를 누릅니다.
- **macOS 11~14:** 응용 프로그램 폴더에서 앱을 Control 키를 누른 채 클릭하고 **열기**를
  고른 다음, 다시 **열기**를 누릅니다.
- **터미널(모든 버전. "손상되었기 때문에 열 수 없습니다"라고 나올 때도 이걸로 풀립니다):**

  ```sh
  /usr/bin/xattr -dr com.apple.quarantine "/Applications/R Television.app"
  ```

한 번 허용하면 그다음부터는 다른 앱처럼 그냥 열립니다.

### Linux

![Linux의 R Television](docs/screenshots/linux.png)

1. 이 프로젝트를 ZIP 파일로 다운로드해 압축을 풉니다(`git`으로 clone해도 됩니다).
2. 압축을 푼 폴더에서 터미널을 열고 다음을 실행합니다.

   ```sh
   cd platforms/linux
   ./install.sh
   ```

   컴파일러와 GTK·curl 개발 패키지 중 없는 것을 설치하고(비밀번호를 물어볼 수 있습니다),
   R Television을 빌드해 홈 폴더에 설치합니다.
3. 프로그램 메뉴에서 **R Television**을 실행하거나 `r-television`을 입력합니다.

Linux Mint 20.3과 Ubuntu 20.04에서 확인했습니다.

### Haiku

![Haiku의 R Television](docs/screenshots/haiku.png)

1. 이 프로젝트를 ZIP 파일로 다운로드해 압축을 풉니다.
2. 압축을 푼 폴더에서 터미널을 열고 다음을 실행합니다.

   ```sh
   cd platforms/haiku
   ./install.sh
   ```

   없는 패키지(`gcc`, `haiku_devel`)는 `pkgman`으로 설치하고, 시스템의 다른 부분은
   건드리지 않습니다.
3. **Deskbar > Applications**에서 **RTelevision**을 실행합니다.

**arm64** Haiku에는 VLC도 FFmpeg도 패키지가 없어서, 2단계 전에 다른 컴퓨터에서 FFmpeg를
빌드해야 합니다. 방법은 [AGENTS.md](AGENTS.md#haiku-arm64-ffmpeg)(영어)에 있습니다.

## 제거

macOS:

```sh
rm -rf "/Applications/R Television.app"
rm -rf ~/Library/Application\ Support/RTelevision
```

Linux(프로젝트 폴더에서):

```sh
cd platforms/linux && make uninstall
```

Haiku:

```sh
rm -rf ~/config/non-packaged/apps/RTelevision ~/config/settings/deskbar/menu/Applications/RTelevision
rm -rf ~/config/non-packaged/data/RTelevision ~/config/settings/RTelevision
```

macOS의 두 번째 줄과 Haiku의 `settings` 폴더에는 채널 목록 캐시와 즐겨찾기가 들어 있습니다.

## 라이선스

MIT이고 [LICENSE](LICENSE)에 있습니다. 함께 들어 있는 VLC와 FFmpeg 라이브러리는 각자의
라이선스를 따르며 [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md)에 정리되어 있습니다.
채널 목록은 [iptv-org/iptv](https://github.com/iptv-org/iptv)의 데이터를 참고하며,
그 라이선스는 iptv-org의 것을 따릅니다.

## AI 활용 고지

이 프로그램은 Claude와 함께 작업해 만들었습니다.
