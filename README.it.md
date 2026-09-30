# <img src="resources/icon/icon_64.png" width="32" height="32" alt=""> R Television

Un lettore per i canali TV gratuiti di tutto il mondo: i canali a sinistra,
l'immagine a destra. L'interfaccia segue la lingua del sistema (inglese,
italiano, giapponese o coreano).

[English](README.md) · Italiano · [日本語](README.ja.md) · [한국어](README.ko.md)

## Cosa serve

| Sistema | Come si installa | Cos'altro serve |
|---|---|---|
| macOS 11 o successivo, Apple Silicon o Intel | si scarica l'app | niente |
| Linux x86_64: Debian, Ubuntu, Linux Mint | si compila con un solo comando | una connessione a internet e, una volta, la tua password |
| Haiku x86_64, x86 a 32 bit o arm64 | si installa con `pkgman` | una connessione a internet |

Non serve installare VLC a mano da nessuna parte: su macOS e sulle distribuzioni
Linux basate su Debian R Television porta il proprio, su Haiku la libreria
multimediale sta dentro il pacchetto oppure la installa `pkgman` insieme ad esso.

## Installazione

### macOS

![R Television su macOS](docs/screenshots/macos.png)

1. Scarica [`R-Television-1.0.0-macOS.dmg`](platforms/macos/dist/R-Television-1.0.0-macOS.dmg).
2. Aprilo e trascina **R Television** in **Applicazioni**.
3. Apri R Television da Applicazioni.

L'app non è firmata con un Developer ID di Apple, perciò la prima volta macOS la
blocca. Basta sbloccarla una volta, nel modo che preferisci:

- **macOS 15 e successivi:** dopo l'avviso apri **Impostazioni di Sistema >
  Privacy e sicurezza**, scorri fino a *"R Television" è stato bloccato* e fai
  clic su **Apri comunque**.
- **Da macOS 11 a 14:** fai clic sull'app in Applicazioni tenendo premuto Ctrl,
  scegli **Apri** e poi di nuovo **Apri**.
- **Dal Terminale, con qualsiasi versione** (risolve anche il messaggio
  *"è danneggiata e non può essere aperta"*):

  ```sh
  /usr/bin/xattr -dr com.apple.quarantine "/Applications/R Television.app"
  ```

Da quel momento si apre come qualsiasi altra app.

### Linux

![R Television su Linux](docs/screenshots/linux.png)

1. Scarica questo progetto come file ZIP e decomprimilo (oppure clonalo con
   `git`).
2. Apri un terminale nella cartella decompressa ed esegui:

   ```sh
   cd platforms/linux
   ./install.sh
   ```

   Lo script installa ciò che manca tra il compilatore e i pacchetti di sviluppo
   di GTK e curl (può chiederti la password), poi compila R Television e lo
   installa nella tua cartella personale.
3. Avvia **R Television** dal menu delle applicazioni, oppure esegui
   `r-television`.

Provato su Linux Mint 20.3 e Ubuntu 20.04.

### Haiku

![R Television su Haiku](docs/screenshots/haiku.png)

R Television è pacchettizzato per ogni architettura di Haiku e VLC non va mai
installato a mano: `pkgman` trova la libreria multimediale dentro il pacchetto
oppure la installa insieme ad esso.

```sh
pkgman add-repo https://pkgman.rainygirl.com/$(getarch -p)
pkgman install rtelevision
```

Sull'immagine x86_gcc2 a 32 bit il pacchetto si chiama `rtelevision_x86`, perché
l'applicazione viene compilata per l'architettura secondaria:

```sh
pkgman install rtelevision_x86
```

Poi avvia **R Television** da **Deskbar > Applications**. L'immagine RENKU arm64
ha già questo repository.

Per compilarlo da sé, scarica il progetto come file ZIP, decomprimilo, apri il
Terminale nella cartella ed esegui:

```sh
cd platforms/haiku
./install.sh
```

I pacchetti mancanti (`gcc`, `haiku_devel`) vengono installati con `pkgman`; il
resto del sistema non viene toccato. Le note di compilazione sono in
[AGENTS.md](AGENTS.md#haiku) (in inglese).

## Disinstallazione

macOS:

```sh
rm -rf "/Applications/R Television.app"
rm -rf ~/Library/Application\ Support/RTelevision
```

Linux (dalla cartella del progetto):

```sh
cd platforms/linux && make uninstall
```

Haiku:

Se installato dal repository:

```sh
pkgman uninstall rtelevision        # rtelevision_x86 on the x86_gcc2 image
rm -rf ~/config/settings/RTelevision
```

Se compilato da sé:

```sh
rm -rf ~/config/non-packaged/apps/RTelevision ~/config/settings/deskbar/menu/Applications/RTelevision
rm -rf ~/config/non-packaged/data/RTelevision ~/config/settings/RTelevision
```

La seconda riga di macOS e la cartella `settings` di Haiku contengono la cache
dei canali e i tuoi preferiti.

## Licenza

MIT, vedi [LICENSE](LICENSE). Le librerie VLC e FFmpeg incluse mantengono le
proprie licenze, elencate in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md).
La lista dei canali proviene da [iptv-org/iptv](https://github.com/iptv-org/iptv)
e segue la licenza di iptv-org.

## Nota sull'uso dell'IA

Questo programma è stato scritto insieme a Claude.
