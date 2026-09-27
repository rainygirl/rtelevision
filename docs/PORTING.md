# 포팅 노트

## 계층

```
  ┌──────────┐  ┌──────────┐  ┌──────────┐
  │ Cocoa UI │  │ GTK3 UI  │  │ BeAPI UI │   ← 플랫폼마다 새로 작성
  └────┬─────┘  └────┬─────┘  └────┬─────┘
       └─────────────┼─────────────┘
                ┌────┴─────┐
                │ shared/  │                 ← 그대로 재사용, 수정 0줄
                └────┬─────┘
           ┌─────────┴─────────┐
      MediaPlayer          HttpClient
      libVLC / Null        libcurl / Haiku netservices
```

`shared/`에는 UI 코드도, `#import`도, 플랫폼 SDK 호출도 없다. OS 차이를 흡수하는
지점은 네 군데뿐이다.

| 경계 | 파일 | macOS | Linux | Haiku |
|---|---|---|---|---|
| 데이터 경로 | `core/Paths.cpp` | `~/Library/Application Support/` | `$XDG_DATA_HOME` | `~/config/settings/` |
| 비디오 출력 | `core/MediaPlayer.h` | `attachVideoView(NSView*)` | `attachVideoView(XID)` | `attachVideoSink(VideoFrameSink*)` |
| HTTP | `core/HttpClient.h` | `CurlHttpClient` | `CurlHttpClient` | `HaikuHttpClient` |
| UI 스레드 복귀 | 프론트엔드 | `dispatch_async(main_queue)` | `g_idle_add` | `BMessenger::SendMessage` |
| 표시 언어 | `core/Strings.h` | `[NSLocale preferredLanguages]` | `LANG` / `LC_ALL` | `BLocaleRoster` |

HLS 릴레이(`core/HlsRelay.cpp`)도 코어에 있다. 화질이 하나뿐인 라이브 재생목록이면
`RelayedMediaPlayer`가 플레이어에 원래 주소 대신 127.0.0.1 주소를 넘긴다. 릴레이는
세그먼트를 HTTP Range 요청 여러 개로 나눠 받고, 다 받은 세그먼트만 재생목록에 올린다.
조각은 약 750KB씩, 연결 수의 1~3배로 나눈다. 둘째 세그먼트부터는 직전 크기로 곧바로
나누고 마지막 조각을 "끝까지"로 요청한다. 절반 이상 끝났는데 오래 걸리는 조각은 쉬는
연결로 복제해 경주시킨다. 연결마다 속도 편차가 커서, 이게 없으면 가장 느린 조각 하나가
세그먼트 전체를 붙잡는다(ABC 1080p 측정: 가장 느린 조각 12~16초 → 8초 이내).
첫 재생목록 요청은 20초 분량이 쌓일 때까지 붙잡는다. 서버가 실시간의 2배 이상으로
빠르면 곧바로 내준다. 소켓은 POSIX API라 세 플랫폼에 같은 코드가 돈다(Haiku는
`-lnetwork`). 끄려면 `RTV_NO_HLS_RELAY=1`, 조정은 `RTV_RELAY_CONNECTIONS`(기본 4)와
`RTV_RELAY_BUFFER`(초, 기본 20)로 한다.

UI 문자열은 `core/Strings.cpp` 한 곳에 영어/이탈리아어/일본어/한국어로 들어 있다.
프론트엔드는 시작할 때 시스템 언어를 읽어 `setLanguage()`를 부르고, 지원하지 않는
언어면 영어가 나온다.

코어 콜백(`MediaPlayer::StateCallback`, `AppController::RefreshCallback`)은 전부
워커 스레드에서 불린다. 프론트엔드가 자기 UI 스레드로 넘긴 뒤에 위젯을 만져야 한다.

백엔드 선택은 플랫폼 Makefile이 소스 파일 하나를 고르는 방식이다
(`shared/common.mk`의 `CORE_SRC_VLC` / `CORE_SRC_NULL`,
`CORE_SRC_HTTP_CURL` / `CORE_SRC_HTTP_HAIKU`). `#ifdef`로 코어를 오염시키지 않는다.

## macOS x86_64 / 유니버설

검증 완료. 코드 변경 없음.

```sh
cd platforms/macos
make ARCHS=x86_64
make ARCHS="arm64 x86_64"
```

`third_party/vlc-macos/`에 받는 dmg가 유니버설이라 dylib·플러그인 346개가 모두
`x86_64 arm64` 팻 바이너리다. 한 트리로 양쪽 슬라이스를 커버한다.
배포 타깃 11.0, 빌드 후 ad-hoc 서명. 외부 배포하려면 Developer ID 서명 + 공증이 필요하다.

## Linux x86_64 — 실행 확인됨

Linux Mint 20.3 (Ubuntu 20.04 기반, 커널 5.4, bspwm + compton)에서 빌드하고 실행했다.

- **코어 전체가 수정 0줄로 컴파일**됐다. macOS와 같은 `CurlHttpClient`를 쓴다.
- 채널 목록을 온라인으로 받아 `~/.local/share/RTelevision/`에 캐시했다.
- **재생 확인** — 스트림 서버와 TCP 연결이 established 상태로 유지되고 화면에 영상이 나온다.
- libVLC는 배포판 .deb에서 추출해 앱에 번들했다(15MB). 시스템 VLC 설치 불필요.

### 걸린 것들

0. **번들 플러그인이 헬퍼 라이브러리를 못 찾는다.** libVLC는 플러그인을 `dlopen` 하는데,
   실행 파일의 RPATH는 *dlopen된 객체의 의존성*까지 커버하지 않는다. 그래서
   `libvlc_xcb_events.so.0`을 못 찾아 **비디오 출력 모듈이 통째로 로드되지 않고 화면이
   검게** 나왔다. glibc는 `LD_LIBRARY_PATH`를 시작 시 한 번만 읽으므로 `setenv` 후
   `execv("/proc/self/exe", argv)`로 한 번 다시 실행한다(`platforms/linux/main.cpp`).
1. **Xlib 매크로 오염.** `<X11/Xlib.h>`가 `None`, `Status`, `Bool`을 `#define` 한다.
   `PlaylistSource::None`이 숫자 상수로 치환돼 컴파일이 깨진다.
   `gdkx.h` 다음에 `#undef None` 등을 넣어야 한다.
2. **RUNPATH는 상속되지 않는다.** 기본 `--enable-new-dtags`로 링크하면 실행 파일에
   RUNPATH가 박히는데, 이건 libvlc가 자기 의존인 libvlccore를 찾을 때 적용되지 않는다.
   `-Wl,--disable-new-dtags`로 RPATH를 내보내야 `$ORIGIN/vlc/lib` 번들이 동작한다.
   `--as-needed` 때문에 `-lvlccore`가 NEEDED에서 빠지는 것도 같이 물린다.
3. **의존성 클로저 폭발.** `apt-cache depends --recurse`를 그대로 쓰면 이미 설치된
   mesa를 지나 LLVM/clang까지 끌고 와 791MB가 된다. **이미 설치된 패키지에서 탐색을
   멈추면** 6개 패키지 15MB로 끝난다. 이후 `ldd`로 실제 필요한 .so만 채우고,
   끝까지 해결되지 않는 플러그인은 버린다.
4. **배포판 libVLC가 오래됐다.** Ubuntu 20.04는 VLC 3.0.9.2다. iptv-org의 일부 HLS
   마스터 플레이리스트에서 자막 렌디션만 반복해서 받고 영상 세그먼트를 전혀 받지 않는다
   (`chunklist_..._sleng.m3u8`만 1600회). 같은 URL이 macOS의 3.0.23에서는 정상이다.
   앱 문제가 아니라 VLC 버전 문제이므로, 채널을 바꾸거나 새 VLC를 쓰면 된다.
5. **컴포지터가 있으면 스크린샷에 영상이 안 잡힌다.** compton이 창을 리다이렉트하므로
   `xwd`가 검은 화면을 준다. XVideo 오버레이(`xcb_xv`)는 컴포지터가 없어도 캡처되지 않는다.
   `RTV_VLC_ARGS="--vout=xcb_x11"`로 오버레이를 끄면 캡처된다. 앱 자체의 문제는 아니다.

## Haiku arm64 — 재생까지 확인됨

### FFmpeg 백엔드의 재생 속도 (2026-09-14 수정)

오디오가 기준 시계라서, 영상 프레임은 사운드카드가 실제로 재생한 만큼 시계가 갈 때까지
기다린다. 예전 디코드 루프는 한 스레드가 패킷을 읽고 디코드하고 영상 프레임 시각까지
그 자리에서 기다렸다. ABC처럼 영상이 같은 시각의 오디오보다 0.5~1.2초 앞서 섞인
스트림에서는, 영상이 기다리는 동안 그 오디오를 읽을 스레드가 없어 시계가 서고, 매 프레임이
대기 상한(5초)을 채웠다. Haiku에서는 "재생 중"인데 화면이 거의 멈춰 있고 CPU는 놀았다.

지금은 영상 패킷을 압축된 채로 큐에 두고 시계보다 0.5초 앞까지만 디코드하며, 오디오는 읽는
즉시 사운드카드로 보낸다. 디코드된 프레임은 12장까지만 들고 있고, 큐가 8초를 넘으면 시계와
상관없이 진행한다. 끊긴 오디오 구간(깨진 AC-3 프레임 등)은 무음으로 채워 시계가 스트림
시각을 따라가게 했다.

macOS에서 같은 코드를 실시간으로 소비하는 가짜 사운드카드에 붙여 비교했다(ABC, 릴레이 경유).
예전 루프는 오디오 0.00 s/s·CPU 0%로 멈췄고, 지금 루프는 오디오 1.01 s/s·CPU 35%다.

HaikuPorts arm64에는 vlc도 ffmpeg도 없다. 그래서 **FFmpeg를 Haiku/arm64로 크로스
빌드하고 libVLC 대신 그 위에 백엔드를 하나 더 만들었다** (`FFmpegMediaPlayer`).

- `MediaPlayer` 인터페이스와 `VideoFrameSink`가 이미 있었기 때문에 UI와 코어는 한 줄도
  바뀌지 않았다. 새 백엔드는 `avformat`으로 URL을 열고, `avcodec`으로 디코딩하고,
  `swscale`로 BGRA로 바꿔 싱크에 넣고, PTS에 맞춰 페이싱한다.
- 빌드는 Haiku 크로스 툴체인(`cross-tools-arm64`)으로 Linux/macOS 쪽에서 한다.
  arm64 Haiku 이미지에는 `make`조차 없으므로 네이티브 빌드는 불가능하다.
  앱 자체는 `install.sh`의 make 없는 경로로 VM에서 컴파일된다.
- 오디오도 붙어 있다. 코어에 `AudioSink`를 하나 더 두고(`MediaPlayer.h`), Haiku 쪽은
  `platforms/haiku/AudioOutput.cpp`에서 `BSoundPlayer`로 구현했다. BSoundPlayer는
  콜백으로 샘플을 **당겨가는** 구조라 링 버퍼를 두고, 디코더가 채우고 콜백이 비운다.
  버퍼가 차면 `queueAudio()`가 막히므로 디코딩 속도가 사운드 카드에 맞춰 저절로 조절된다.
- **동기화는 오디오가 마스터 클럭**이다. `AudioSink::playedMicros()`가 사운드 카드가
  실제로 재생한 양을 돌려주고, 영상 프레임은 자기 PTS가 그 시계에 도달할 때까지 기다린다.
  오디오가 없는 스트림에서는 벽시계로 되돌아간다.

## Haiku arm64 — UI 실행 확인됨

QEMU(hvf, `-cpu host`)에서 도는 Haiku R1~beta6 arm64(hrev99002)에서 빌드하고 실행했다.

확인된 것:

- **코어 11개 파일이 수정 0줄로 컴파일**됐다. 경고도 없다.
- **채널 목록 다운로드 성공** — `HaikuHttpClient`(libnetservices)로
  `https://iptv-org.github.io/iptv/index.m3u` 11,035채널을 받아 캐시에 저장.
  같은 시점에 `pkgman` 자체 다운로드는 계속 실패했는데 이쪽은 됐다.
- **오프라인 우선 동작** — 재실행 시 캐시를 먼저 읽어 즉시 목록이 뜨고,
  백그라운드 갱신은 304로 끝난다. 상태줄에 `오프라인 캐시`로 표시된다.
- **카테고리 트리·검색·국기 접두사**가 macOS와 동일하게 동작한다.
- `install.sh`가 **make 없이** 빌드·설치까지 끝낸다.

확인되지 않은 것:

- **재생.** HaikuPorts arm64 저장소에 `vlc` / `vlc_devel`이 없다(x86_64에는 있다).
  빌드는 `NullMediaPlayer`로 떨어지고 재생 시도는
  "no media backend on this build"를 보고한다. 목록·검색·즐겨찾기·캐시는 전부 정상.
- 따라서 `VideoView`의 `BBitmap` 블릿 경로(= `VideoFrameSink` 구현)는
  **아직 실제 프레임을 받아 본 적이 없다.** x86_64 Haiku에서 `vlc_devel`을 깔고
  `make VLC=1`로 빌드하면 여기가 첫 검증 지점이다. 색이 뒤집히면
  `VlcMediaPlayer::formatCb`의 `memcpy(chroma, "RV32", 4)` 한 줄을 바꾼다.

### 포팅하면서 실제로 걸린 것들

빌드된 스캐폴드가 처음부터 돌지는 않았다. 기록해 둔다.

1. `BWindow::SetFullScreen` / `IsFullScreen`은 **없다.** 직접 구현해야 한다 —
   프레임을 저장하고 `SetLook(B_NO_BORDER_WINDOW_LOOK)` + `BScreen::Frame()`으로 리사이즈
   (`MainWindow::ToggleFullScreen`).
2. `BLayoutBuilder::Split<>`는 `(BWindow*, orientation)`을 받지 않는다.
   `BSplitView`를 직접 만들어 `AddChild` 하고, 창에는 `Group<>` 빌더로 붙인다.
3. `BUrl(const char*)`는 **오버로드가 모호하다.** `BUrl(url, false)`처럼
   encode 플래그를 명시해야 컴파일된다.
4. `libnetservices.a`는 `BPrivate::HashString`을 쓰므로 **`-lshared`가 같이 필요**하다.
   빼면 `NetworkCookieJar.o`에서 undefined reference가 쏟아진다.
5. `BOutlineListView::AddUnder()`는 상위 항목 **바로 뒤에** 끼워 넣는다.
   하위 카테고리와 자기 채널을 섞어 넣으면 순서가 뒤집힌다.
   `AddItem()`으로 깊이 우선 순서대로 append하고 계층은 `OutlineLevel`에 맡기는 편이 맞다.
6. 접힘 상태는 **항목을 만들 때** 정해야 한다. 다 넣고 나서 `Expand()`를 호출하면
   접힌 부모 밑의 행이 보이는 목록의 엉뚱한 위치로 들어간다.
7. `~/config/apps`는 packagefs라 **읽기 전용**이다. 사용자 바이너리는
   `~/config/non-packaged/apps/RTelevision/`로 간다. 런타임 로더는 바이너리 옆은
   보지 않고 `<바이너리 디렉터리>/lib`만 뒤지므로, 함께 넣는 라이브러리는 `lib/`에
   둔다. rpath가 없는 VLC 플러그인이 libdvbpsi 같은 의존 라이브러리를 찾는 유일한 길이다.
8. 최소 이미지에는 `make`·`grep`·`sed`·`awk`·`tar`·`which`가 **없다.**
   `install.sh`가 make 없이도 빌드하도록 짠 이유다.
9. 국기 이모지는 Haiku 기본 폰트에 글리프가 없어 `🇰🇷` 대신 `KR`이 네모 두 칸으로
   그려진다. 읽는 데 지장은 없어 그대로 뒀다.

### Haiku 빌드 의존성

```sh
pkgman install gcc haiku_devel      # 필수 (이미지에 이미 있을 수 있음)
pkgman install make                 # 있으면 편함, 없어도 install.sh가 처리
pkgman install curl_devel           # 있으면 libcurl 사용
pkgman install vlc_devel            # x86_64 전용. 있으면 재생 가능
```

### 먼저 CLI부터

UI 전에 `make cli`로 헤드리스 도구가 도는지 보면 코어(파싱·HTTP·캐시)가 멀쩡한지
바로 갈린다. 여기까지 통과하면 남은 건 순수하게 UI 문제다.

## 아이콘

버튼 아이콘은 플랫폼마다 사정이 다르다.

| | 출처 | 단색 유지 방법 |
|---|---|---|
| macOS | SF Symbols | 템플릿 이미지는 강조색으로 칠해진다. `NSImageSymbolConfiguration configurationWithHierarchicalColor:` 로 라벨 색을 지정하고 `template = NO` |
| Linux | 아이콘 테마 | `-symbolic` 이름을 쓰면 정의상 단색이고 전경색을 따라간다 |
| Haiku | 없음 | `Icons.cpp`가 BBitmap에 직접 그린다. PNG를 쓰면 translators까지 딸려오므로 도형으로 그리는 편이 가볍다 |

Haiku에서 아이콘 버튼을 쓰려면 `BControl::SetIcon(const BBitmap*)`에 넘긴다.
작은 크기(16-20px)에서는 모서리 브래킷 같은 도형의 팔 길이를 충분히 짧게 잡아야
가운데가 붙어 네모로 뭉개지지 않는다.

또 `BStringView`의 최소 너비는 텍스트 전체 폭이라, 좁은 화면에서 같은 줄의
음량 슬라이더와 전체화면 버튼을 창 밖으로 밀어낸다. `SetExplicitMinSize`로
줄어들 수 있게 하고 `SetTruncation(B_TRUNCATE_END)`을 주어야 한다.

## 빌드 주의

플랫폼 Makefile은 `-MMD -MP`로 헤더 의존성을 추적한다. 이걸 넣기 전에 `ChannelIndex`에
멤버를 하나 추가했더니, 그 헤더를 포함한 오브젝트가 다시 빌드되지 않아 구조체 레이아웃이
어긋났고 앱이 vtable 호출에서 죽었다. 헤더를 고친 뒤 이상하게 죽으면 `make clean`부터
확인할 것.

## libVLC 번들링

앱이 시스템 VLC에 의존하지 않도록 플랫폼마다 `fetch-libvlc.sh`가 libVLC를 가져와
`third_party/vlc-<platform>-<arch>/`에 놓고, Makefile이 실행 파일 옆에 복사한다.

| 플랫폼 | 출처 | 결과 |
|---|---|---|
| macOS | VideoLAN 공식 universal dmg | 플러그인 346개, 185MB. 번들 안 `Contents/MacOS/{lib,plugins}` |
| Linux | 배포판 .deb (`apt-get download` + `dpkg -x`, root 불필요) | 플러그인 353개, 15MB. `$ORIGIN/vlc/lib` |
| Haiku x86_64 | HaikuPorts `.hpkg`를 받아 `package extract`로 해제 (설치 안 함) | 플러그인 310개, 17MB. 실행 파일 옆 + `$ORIGIN` |
| Haiku arm64 | FFmpeg를 직접 크로스 빌드 (`scripts/build-ffmpeg-haiku.sh`) | 4.7MB. 실행 파일 옆 + `$ORIGIN` |

arm64 Haiku만 예외인 이유는 단순하다. HaikuPorts의 arm64 저장소에는 패키지가 50개뿐이고
vlc도 ffmpeg도 없다. `pkgman install vlc`가
`Failed to find a match for "vlc": Name not found`로 끝난다. 상류에 바이너리가 존재하지
않으므로 가져올 대상이 없다. 이 조합은 `NullMediaPlayer`로 빌드되고, 재생을 시도하면
그 사실을 화면에 표시한다. 나머지 기능은 전부 정상이다.

직접 포팅하려면 HaikuPorts 레시피(vlc + ffmpeg + 30여 개 의존성)를 arm64 크로스
툴체인으로 빌드해야 한다. 그게 끝나면 `fetch-libvlc.sh`가 그대로 동작한다.

### Haiku에서 설치 없이 번들하기

`.hpkg`는 그냥 아카이브라서 활성화하지 않고도 열 수 있다.

1. `echo no | pkgman install vlc vlc_devel` — pkgman은 계획만 출력하고 아무 것도 하지 않는다.
   이미 설치된 패키지는 계획에서 빠지므로 받을 목록이 자연스럽게 최소화된다.
2. `pkgman list-repos`에서 HaikuPorts base-url을 얻어 `<base>/packages/<name>-<arch>.hpkg`를 받는다.
   Qt5·GStreamer는 우리가 쓰지 않는 플러그인용이라 건너뛴다(용량의 대부분).
3. `package extract -C <dir> <file.hpkg>`로 푼다.
4. `lib/`, `develop/lib/`(링크용 심볼릭 링크), `lib/vlc/plugins/`를 모아 트리를 만들고,
   `readelf -d`로 NEEDED를 훑어 시스템에도 트리에도 없는 라이브러리를 채운다.
   끝까지 못 채운 플러그인은 버린다.

주의할 점 두 가지:

- Haiku의 런타임 로더는 **실행 파일 디렉터리를 자동으로 찾지 않는다.** `-Wl,-rpath,'$ORIGIN'`이 필요하다.
- rpath는 **의존성의 의존성까지 전파되지 않는다.** libvlc가 libvlccore를 찾지 못하므로
  `-lvlccore`를 실행 파일에 직접 링크해 로더가 앱의 rpath로 찾게 해야 한다.
  (Linux에서 RUNPATH 때문에 겪은 것과 같은 문제다.)

## PowerPC Mac (Mac OS X 10.4 Tiger) — 네이티브 포팅 진행 중

iMac G4 (PowerPC 7450 1 GHz, 1 GB, 디스크 여유 5 GB)에 G4 단독으로 도는 앱을
올리는 중이다. 다른 기기가 필요 없어야 한다.

### 처음 판단이 틀렸던 것

코어를 C++17로 보고 "C++98로 내리려면 250곳을 고쳐야 한다"고 판단했는데,
실제로 재 보니 코어는 **엄격한 C++11 기준으로도 위반이 0건**이다. C++17로
컴파일하고 있을 뿐 C++17 기능을 쓰지 않는다.

이게 난이도를 완전히 바꾼다. 필요한 것은 최신 GCC가 아니라 **GCC 4.7**이고,
4.7은 C로 작성된 마지막 GCC라 Tiger의 GCC 4.0.1로 직접 빌드할 수 있다.
크로스 툴체인도, 코어 수정도 필요 없다.

`shared/common.mk`의 `CORE_CXXSTD`로 플랫폼이 표준을 고른다. PPC만 `c++11`이고
나머지는 그대로 `c++17`이다.

### 막는 것과 해법

| 항목 | 해법 |
|---|---|
| 컴파일러 | GCC 4.7.4를 기기에서 직접 빌드. `--disable-bootstrap`, C/C++만 |
| HLS 없음 | 릴레이의 평문 MPEG-TS 모드. VLC는 HLS를 볼 일이 없다 |
| AES-128 | 평문 TS를 받는 재생기는 키를 못 보므로 릴레이가 직접 복호화 (`Aes128.cpp`) |
| TLS | OpenSSL 1.1.1w와 curl 7.87을 기기에서 빌드 |
| libVLC API | 0.9는 호출마다 `libvlc_exception_t`를 받는다. `VlcLegacyMediaPlayer.cpp` |
| 영상 출력 | `libvlc_drawable_t`는 `int`다. 32비트라 NSView\*를 넣을 수 있다 |
| UI | Cocoa 헤더는 Apple GCC 4.0.1이 안다. 순수 Objective-C로 쓰고 C 경계에서 만난다 |

### 컴파일러 두 개를 섞는 방법

Tiger의 Cocoa 헤더는 Apple GCC 4.0.1이 가장 잘 다루고, 코어는 GCC 4.7이
필요하다. 둘은 C++ ABI를 공유할 수 없으므로 **경계를 C로 둔다.**

- 코어와 `UiBridge.cpp`: GCC 4.7 (C++11)
- `UiBridge.h`: C89로도 유효한 헤더
- Cocoa 프런트엔드: 순수 Objective-C, Apple GCC 4.0.1
- 링크: GCC 4.7의 g++ (libstdc++가 따라온다) + `-framework Cocoa`

채널 트리는 브리지가 **평탄한 행 배열로 만들어 넘긴다.** NSOutlineView는 자식을
한 단계씩 되물어서 경계를 너무 자주 넘는다. 들여쓴 목록이 같은 그림을 그린다.

### 상태 — 완료

G4 단독으로 동작한다. 다른 기기는 필요 없다.

- [x] 코어가 C++11임을 확인 (엄격 모드 위반 0)
- [x] GCC 4.7.4를 기기에서 빌드 (2시간 7분)
- [x] OpenSSL 1.0.2u + curl 7.87 빌드
- [x] 릴레이 평문 MPEG-TS 모드 + AES-128 복호화
- [x] `VlcLegacyMediaPlayer.cpp`
- [x] Cocoa 프런트엔드와 `UiBridge`
- [x] rtv-cli가 HTTPS로 채널 목록 10,944개 수신
- [x] 앱 번들 빌드, 설치, 재생 확인

### 컴파일러를 세 개 쓴다

| 대상 | 컴파일러 | 이유 |
|---|---|---|
| C++ 코어 | GCC 4.7.4 | C++11이 필요하다 |
| Cocoa 프런트엔드 | Apple GCC 4.0.1 | Tiger의 Cocoa 헤더를 아는 유일한 컴파일러 |
| OpenSSL, curl | Apple GCC 4.0.1 | `-arch ppc`는 Apple 확장이라 GCC 4.7이 거부한다 |

셋 다 C 경계에서만 만나므로 C++ ABI가 섞이지 않는다. libstdc++와 libgcc는 정적으로
넣어서 앱이 `DYLD_LIBRARY_PATH` 없이 뜬다.

### GCC 4.7 때문에 고친 것

코어는 C++11이지만 GCC 4.7은 C++11을 다 구현하지 못했다.

| 걸린 것 | 대응 |
|---|---|
| `std::map::emplace` (4.8부터) | `insert(make_pair(...))` |
| 소멸자의 암묵적 `noexcept`를 추론 못 함 | 파생 클래스 다섯 곳에 명시 |
| `thread_local` (4.8부터) | pthread 키 |
| 32비트 PPC에 64비트 원자 연산 없음 | 세대 카운터를 32비트로 |

모두 표준 C++11이라 다른 세 플랫폼 빌드에는 영향이 없다.

### Tiger와 VLC 0.9 때문에 고친 것

- **`makedepend`가 없다.** OpenSSL의 `make depend`를 건너뛴다. 깨끗한 트리에는 필요 없다.
- **Perl이 5.8.6이다.** OpenSSL 1.1.1은 5.10을 요구한다. 1.0.2u를 쓴다. TLS 1.2는 된다.
- **libcurl은 `CURL_CA_BUNDLE`을 읽지 않는다.** 그건 명령행 도구의 동작이다. 코어가
  그 변수를 읽어 `CURLOPT_CAINFO`로 넘기고, 앱은 번들 안의 `cacert.pem`을 가리킨다.
- **VLC 0.9에 `--network-caching`이 없다.** 모르는 옵션 하나에 `libvlc_new`가 통째로
  실패한다. `--http-caching`으로 바꿨다.
- **`libvlc_drawable_t`는 `int`다.** 32비트라 NSView 포인터가 들어간다.
- **libvlc의 미정의 심볼이 `@loader_path/../lib/libvlccore.0.dylib` 소속으로 기록되어
  있다.** 링커가 따라갈 수 없는 경로라 `-dylib_file`로 실제 파일에 대응시킨다.
  실행 시에는 번들 배치가 VLC.app과 같아서 저절로 풀린다.
- **`libmacosx_plugin`을 빼야 한다.** VLC 자체 GUI 모듈인데, 로드되는 것만으로 EyeTV
  분산 알림 옵서버를 등록하고 그 핸들러가 널 객체를 건드려 프로세스를 죽인다.
  영상 출력은 `minimal_macosx`가 맡는다.
- **`libzvbi`도 빼야 한다.** libiconv 7로 빌드되었는데 Tiger는 5다. VLC.app에서도 깨진다.

### 화면 출력에서 걸린 것

libVLC 0.9의 OpenGL 출력은 `CGDisplayUsesOpenGLAcceleration`을 확인하고 거짓이면
바로 포기한다. 이 iMac G4에는 Quartz Extreme이 없어서 해당한다. 그러면 libVLC는
후보를 훑다가 `caca`, 즉 ASCII 아트 출력으로 떨어진다.

그래서 `vmem` 출력을 쓴다. 디코딩 결과를 메모리 버퍼에 받아 프런트엔드가 직접
그리는 방식이고, Haiku가 쓰는 `VideoFrameSink`와 같은 경로다. vmem은 설정을
인스턴스 생성 시점에만 읽으므로(`config_GetPsz`는 객체 변수가 아니라 설정을 본다)
버퍼 크기가 고정이고, 뷰가 레터박스로 맞춘다.

빅엔디언에서 RV24의 바이트 순서는 R, G, B가 아니라 **B, G, R**이다. 프레임당 한 번
교환한다.

### 재생 속도

700 MHz G4에서 480p H.264는 아슬아슬하다. 처음에는 프레임 지연이 분당 수백 건이었다.

| 설정 | 결과 |
|---|---|
| 옵션 없음 | 지연 다수 |
| `--ffmpeg-skiploopfilter=4 --ffmpeg-hurry-up --skip-frames` | 지연 0, 화면이 블록으로 무너짐 |
| `--ffmpeg-skiploopfilter=1 --drop-late-frames` | 지연 0, 화질 정상 |

`--ffmpeg-hurry-up`과 `--skip-frames`는 디코더가 **참조 프레임까지** 버리게 해서
다음 키 프레임까지 블록이 남는다. 참조되지 않는 프레임의 디블로킹만 건너뛰고
(`skiploopfilter=1`), 늦은 그림은 출력 단계에서 버리는(`--drop-late-frames`) 쪽이
디코딩 결과를 해치지 않으면서 같은 효과를 낸다.

프런트엔드 쪽도 프레임마다 `NSBitmapImageRep`을 새로 만들지 않고 캐시하며,
확대는 `NSImageInterpolationNone`으로 한다.

### 그 밖에 걸린 것

- `NSTableColumn`은 기본이 편집 가능이라 더블클릭이 이름 변경으로 들어간다.
  `setEditable:NO`가 필요하다.
- `screencapture`는 디스플레이가 절전이면 종료 코드 0으로 아무것도 쓰지 않는다.
  키 이벤트로 깨운 뒤에야 동작한다.
- 이 기기는 GUI 스크립팅이 꺼져 있어 System Events로 UI 요소에 접근할 수 없다.
  프로세스를 앞으로 가져오는 것은 되고, 클릭은 Quartz 이벤트로 보내야 한다.

### 측정값

| 항목 | 값 |
|---|---|
| 채널 목록의 HLS 비율 | 10,603 / 10,944 |
| 720x576 H.264 재생 | 됨. 45초에 프레임 스킵 44회 |
| 720p 이상 | 불가 |
| 표본 15채널 중 재생됨 (AES 복호화 후) | 7 |

## 다른 플랫폼

- **Windows**: `attachVideoView`에 `set_hwnd` 분기가 이미 있다.
  `Paths.cpp`에 `%APPDATA%` 분기 추가 필요.
- **다른 Linux 배포판**: GTK 프론트엔드는 그대로 쓰고 `fetch-libvlc.sh`만
  해당 패키지 관리자용으로 바꾸거나, `make VLC_SYSTEM=1`로 배포판 libvlc에 링크한다.
