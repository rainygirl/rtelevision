#include "Strings.h"

#include <cassert>
#include <cstdlib>

namespace tv {
namespace {

struct Entry {
    Str id;
    const char* en;
    const char* it;
    const char* ja;
    const char* ko;
};

// Keep in the same order as the Str enum; the rows carry their own id so a
// mistake shows up as a failed assertion rather than as wrong text.
const Entry kTable[] = {
    { Str::AppName, "R Television", "R Television", "R Television", "R Television" },
    { Str::SearchPlaceholder, "Search channels, groups, countries",
      "Cerca canali, gruppi, paesi", "チャンネル / グループ / 国を検索",
      "채널 / 그룹 / 국가 검색" },
    { Str::ColumnChannel, "Channel", "Canale", "チャンネル", "채널" },
    { Str::TabCategory, "Category", "Categoria", "カテゴリ", "카테고리" },
    { Str::TabCountry, "Country", "Paese", "国", "국가" },
    { Str::Favorites, "Favorites", "Preferiti", "お気に入り", "즐겨찾기" },
    { Str::SelectChannel, "Pick a channel", "Scegli un canale", "チャンネルを選んでください",
      "채널을 선택하세요" },

    { Str::Opening, "Opening...", "Apertura...", "接続中...", "여는 중..." },
    { Str::Buffering, "Buffering", "Buffering", "バッファ中", "버퍼링" },
    { Str::Playing, "Playing", "In riproduzione", "再生中", "재생 중" },
    { Str::Paused, "Paused", "In pausa", "一時停止", "일시정지" },
    { Str::Stopped, "Stopped", "Fermo", "停止", "정지됨" },
    { Str::PlaybackFailed, "Playback failed", "Riproduzione non riuscita", "再生に失敗しました",
      "재생 실패" },
    { Str::UnknownError, "unknown error", "errore sconosciuto", "原因不明のエラー",
      "알 수 없는 오류" },
    { Str::CannotPlay, "Cannot play this channel", "Impossibile riprodurre questo canale",
      "このチャンネルは再生できません", "재생할 수 없습니다" },
    { Str::NoBackend, "No playback backend available", "Nessun backend di riproduzione disponibile",
      "再生バックエンドがありません", "재생 백엔드를 사용할 수 없습니다" },
    { Str::NoBackendHint, "No playback backend (libVLC not installed)",
      "Nessun backend di riproduzione (libVLC non installato)",
      "再生バックエンドがありません (libVLC 未導入)",
      "재생 백엔드가 없습니다 (libVLC 미설치)" },
    { Str::BackendInitFailed, "Could not start the playback backend",
      "Impossibile avviare il backend di riproduzione", "再生バックエンドを起動できませんでした",
      "재생 백엔드를 초기화하지 못했습니다" },

    { Str::Online, "online", "in linea", "オンライン", "온라인" },
    { Str::OfflineCache, "offline cache", "cache locale", "オフラインキャッシュ",
      "오프라인 캐시" },
    { Str::BundledSnapshot, "bundled snapshot", "copia inclusa", "内蔵スナップショット",
      "내장 스냅샷" },
    { Str::NoPlaylist, "none", "nessuna", "なし", "없음" },
    { Str::Downloading, "Downloading...", "Scaricamento...", "ダウンロード中...",
      "다운로드 중..." },
    { Str::RefreshFailed, "Update failed", "Aggiornamento non riuscito", "更新に失敗しました",
      "갱신 실패" },
    { Str::ChannelCount, "%s of %s channels", "%s di %s canali", "%s / 全 %s チャンネル",
      "%s개 / 총 %s개" },

    { Str::TipPlayPause, "Play / Pause", "Riproduci / Pausa", "再生 / 一時停止",
      "재생 / 일시정지" },
    { Str::TipStop, "Stop", "Ferma", "停止", "정지" },
    { Str::TipFavorite, "Favorite", "Preferito", "お気に入り", "즐겨찾기" },
    { Str::TipAddFavorite, "Add to favorites", "Aggiungi ai preferiti", "お気に入りに追加",
      "즐겨찾기 추가" },
    { Str::TipRemoveFavorite, "Remove from favorites", "Togli dai preferiti",
      "お気に入りから削除", "즐겨찾기 해제" },
    { Str::TipFullScreen, "Full screen", "Schermo intero", "全画面", "전체 화면" },
    { Str::TipMute, "Mute", "Silenzia", "ミュート", "음소거" },
    { Str::TipUnmute, "Unmute", "Riattiva l'audio", "ミュート解除", "음소거 해제" },

    { Str::MenuAbout, "About R Television", "Informazioni su R Television",
      "R Television について", "R Television 정보" },
    { Str::MenuOpenCacheFolder, "Open Cache Folder", "Apri la cartella della cache",
      "キャッシュフォルダを開く", "캐시 폴더 열기" },
    { Str::MenuQuit, "Quit R Television", "Esci da R Television", "R Television を終了",
      "R Television 종료" },
    { Str::MenuPlayback, "Playback", "Riproduzione", "再生", "재생" },
    { Str::MenuPlayPause, "Play / Pause", "Riproduci / Pausa", "再生 / 一時停止",
      "재생 / 일시정지" },
    { Str::MenuStop, "Stop", "Ferma", "停止", "정지" },
    { Str::MenuMute, "Mute", "Silenzia", "ミュート", "음소거" },
    { Str::MenuNextChannel, "Next Channel", "Canale successivo", "次のチャンネル", "다음 채널" },
    { Str::MenuPreviousChannel, "Previous Channel", "Canale precedente", "前のチャンネル",
      "이전 채널" },
    { Str::MenuToggleFavorite, "Toggle Favorite", "Aggiungi o togli dai preferiti",
      "お気に入りを切り替え", "즐겨찾기 토글" },
    { Str::MenuChannels, "Channels", "Canali", "チャンネル", "채널" },
    { Str::MenuSearch, "Search Channels", "Cerca canali", "チャンネルを検索", "채널 검색" },
    { Str::MenuRefresh, "Refresh List", "Aggiorna la lista", "一覧を更新", "목록 새로고침" },
    { Str::MenuWindow, "Window", "Finestra", "ウインドウ", "윈도우" },
    { Str::MenuFullScreen, "Full Screen", "Schermo intero", "全画面", "전체 화면" },
    { Str::MenuMinimize, "Minimize", "Riduci a icona", "しまう", "최소화" },
};

static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<size_t>(Str::Count),
              "the string table and the Str enum are out of step");

Language gLanguage = Language::English;

}  // namespace

namespace {

// Only the leading language subtag matters: "ko", "ko_KR", "ko-KR.UTF-8".
std::string primarySubtag(const std::string& tag) {
    std::string code;
    for (char c : tag) {
        if (c == '_' || c == '-' || c == '.' || c == '@') break;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        code.push_back(c);
    }
    return code;
}

}  // namespace

Language languageFromTag(const std::string& tag) {
    const std::string code = primarySubtag(tag);
    if (code == "it") return Language::Italian;
    if (code == "ja") return Language::Japanese;
    if (code == "ko") return Language::Korean;
    return Language::English;
}

bool hasTranslation(const std::string& tag) {
    const std::string code = primarySubtag(tag);
    return code == "en" || code == "it" || code == "ja" || code == "ko";
}

Language detectLanguage() {
    const char* names[] = { "LC_ALL", "LC_MESSAGES", "LANG" };
    for (const char* name : names) {
        const char* value = std::getenv(name);
        if (value && *value) return languageFromTag(value);
    }
    return Language::English;
}

void setLanguage(Language value) { gLanguage = value; }

Language language() { return gLanguage; }

const char* str(Str id) {
    const Entry& entry = kTable[static_cast<size_t>(id)];
    assert(entry.id == id);
    switch (gLanguage) {
        case Language::Italian: return entry.it;
        case Language::Japanese: return entry.ja;
        case Language::Korean: return entry.ko;
        case Language::English: break;
    }
    return entry.en;
}

std::string format(Str id, const std::string& first, const std::string& second) {
    const std::string pattern = str(id);
    std::string out;
    const std::string* next[] = { &first, &second };
    size_t used = 0;
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '%' && i + 1 < pattern.size() && pattern[i + 1] == 's' && used < 2) {
            out += *next[used++];
            ++i;
        } else {
            out.push_back(pattern[i]);
        }
    }
    return out;
}

}  // namespace tv
